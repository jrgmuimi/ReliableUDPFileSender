#include <stdio.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include "UnreliableChannel.h" // For unreliable sending/receiving. See the associated C and Header file.
// Make sure to replace the randomization when finally cleaning up. Remove print statements

#define HEADER_BYTES 16 // Amount of header bytes

static FILE* fptr = NULL; // Pointer to the file we are writing to
static FILE* write_log = NULL; // Log file

static uint32_t expectedSeq  = 0; // The expected sequence number of the next arriving packet
static uint32_t lastSeq  = 0; // The last sequence number we sent back as an ACK

static struct sockaddr senderAddr = {0}; // Use this to store the address of the sender so we can send ACKS back to
static socklen_t addrLen = 0; // Length of address/sockaddr that is returned back from the recvfrom ^

static int isLogging = 0;

static void writeFile(uint8_t* buffer) // Write to the file based on the specified buffer/packet provided
{
	uint32_t length = 0;
	memcpy(&length, buffer+8, sizeof(uint32_t)); // Copy the length from the packet
	length = ntohl(length); // Convert to native endianness
	fwrite(buffer+HEADER_BYTES, sizeof(uint8_t), (length-HEADER_BYTES), fptr); // Write from the 16th byte for len-HEADER bytes
}

static void printPack(uint8_t* packet)
{ // Print header fields of the packet
	uint32_t type = 0;
	uint32_t seqNum = 0;
	uint32_t length = 0;
	uint32_t checksum = 0;

	memcpy(&type, packet, sizeof(uint32_t));
	memcpy(&seqNum, packet+4, sizeof(uint32_t));
	memcpy(&length, packet+8, sizeof(uint32_t));
	memcpy(&checksum, packet+12, sizeof(uint32_t));

	char* typeStr = ntohl(type) ? ((ntohl(type)==1) ? "DATA" : "FIN") : "ACK";

	if (isLogging) { fprintf(write_log, "type=%s; seqNum=%u; length=%u; checksum=%x\n\n", typeStr, ntohl(seqNum), ntohl(length), ntohl(checksum)); }
}

static unsigned int crc32(uint8_t* message, int len)
{ // stackoverflow.com/questions/21001659/crc32-algorithm-implementation-in-c-without-a-look-up-table-and-with-a-public-li
	unsigned int byte, crc, mask;
	crc = 0xFFFFFFFF;

	for(int i = 0; i < len; i++) {
		byte = message[i]; // Get next byte
		crc = crc ^ byte;
		for(int j = 7; j >= 0; j--) { // Do eight times
			mask = -(crc & 1);
			crc = (crc >> 1) ^ (0xEDB88320 & mask);
		}
	}
	return ~crc;
}

static void generic_send(int socket, uint32_t seqNum) // Here we will create the ACK packet
{
	uint32_t type = 0; // The type of ACK is 0, DATA is 1
	uint32_t length = 16; // There is no data/content to be sent, we only need the header bytes, hence 16 length

	uint8_t* message = calloc(12, sizeof(uint8_t)); // We need to create a checksum with the other 3 headers

	memcpy(message, &type, sizeof(uint32_t)); // First the type, copy it to the message
	memcpy(message+4, &seqNum, sizeof(uint32_t)); // Then copy the seqnum
	memcpy(message+8, &length, sizeof(uint32_t)); // Finally copy the length

	unsigned int checksum = crc32(message, 12); // Acquire the checksum

	uint8_t* packet = calloc(16, sizeof(uint8_t)); // Now we finally have to make the packet

	type = htonl(type); // Convert all these sensitive integers to network order. Endianness could affect them
	seqNum = htonl(seqNum);
	length = htonl(length);
	checksum = htonl(checksum); // The checksum as well

	memcpy(packet, &type, sizeof(uint32_t)); // Finally copy everything over to the page
	memcpy(packet+4, &seqNum, sizeof(uint32_t)); // Each field is 4 bytes
	memcpy(packet+8, &length, sizeof(uint32_t));
	memcpy(packet+12, &checksum, sizeof(uint32_t));
	
	if (isLogging) { fprintf(write_log, "Packet sent; "); printPack(packet); }
	//send_packet(socket, packet, 16, &senderAddr, addrLen);  // For unreliable sending. Check UnreliableChannel
	sendto(socket, packet, 16, 0, &senderAddr, addrLen); // Finally send the packet
	free(message); // We no longer need the message or the packet
	free(packet); // Free them
}

static int checkChecksum(uint8_t* buffer, unsigned int* calc_checksum)
{
	uint32_t type = 0; // The checksum will only consist of 12 bytes
	uint32_t seqNum = 0; // Type | SeqNum | Length, plus the actual
	uint32_t length = 0; // data or content we're sending
	uint32_t checksum = 0; // checksum from packet

	memcpy(&type, buffer, sizeof(uint32_t)); // Get the type from the buffer
	memcpy(&seqNum, buffer+4, sizeof(uint32_t)); // Get the seqNum from the buffer
	memcpy(&length, buffer+8, sizeof(uint32_t)); // Get the length from the buffer
	memcpy(&checksum, buffer+12, sizeof(uint32_t)); // Copy the checksum from the packet so we can compare it later

	type = ntohl(type); // These bytes are in network order, so convert them to native endianness
	seqNum = ntohl(seqNum);
	length = ntohl(length);

	uint32_t data_len = (length <= 1472) ? (length-HEADER_BYTES) : 1456; // Copy the correct number of bytes

	uint8_t* data = NULL;
	if(data_len) { // if not finality
		data = calloc(data_len, sizeof(uint8_t)); // Get the actual data/content
		memcpy(data, buffer+16, data_len); // Data starts at the 16th byte and has len subtracted from Header bytes
	}

	uint8_t* message = calloc(data_len+12, sizeof(uint8_t)); // Finally, we need to assemble all our fields
	memcpy(message, &type, sizeof(uint32_t)); // For the final checksum to be calculated. First insert the type
	memcpy(message+4, &seqNum, sizeof(uint32_t)); // Then the seq number
	memcpy(message+8, &length, sizeof(uint32_t)); // Then the length
	if(data_len) { memcpy(message+12, data, data_len); } // Finally the data

	unsigned int crc = crc32(message, data_len+12); // Calculate the checksum from the bytes
	*calc_checksum = crc;
	free(data); // We no longer need the data or the message
	free(message); // So free them
	return (crc == ntohl(checksum)); // Recall the checksum was converted to network byte order, so convert back b4 comparing
}

static int checkSeq(int socket, uint8_t* buffer) // Check the sequence number of the arriving packet.
{ // In circumstances when an ACK sent to the Sender is lost, the Sender will retransmit the same packet thinking the Receiver didn't receive it
	// even though the Receiver might be on the next expected sequence number. So, we will send the lastAck to the Sender
	// in this scenario to satisfy the Sender. Only then will we continue.

	// Get the type,seq from the buffer/packet and convert to native endian
	uint32_t type = 0; memcpy(&type, buffer, sizeof(uint32_t)); type = ntohl(type); 
	uint32_t seq = 0; memcpy(&seq, buffer+4, sizeof(uint32_t)); seq = ntohl(seq); 

	if (seq == expectedSeq) {
		if (isLogging) { fprintf(write_log, "Provided seqNum and expected seqNum match; Sending ACK\n"); }
		generic_send(socket, expectedSeq); // If our current received seq matches the expectedSeq then we can safely send
		if(type==2) { return 0; }
		writeFile(buffer); // We can also write the packet to the file 
		lastSeq = expectedSeq; // Since we've ACKed this file, set it to the lastACK
		expectedSeq+=1; // And increase the expected ACK to the next ACK in sequence we're expecting
	} else { 
		if (isLogging) { 
			fprintf(write_log, "Mismatch between provided and expected seqNum (Seq: %u, Expected Seq: %u)\n", seq, expectedSeq);
			fprintf(write_log, "Resending ACK for provided seqNum=%u...\n\n", seq);
		}
		generic_send(socket, lastSeq); 
	} // If it doesn't match for the reasons listed above, then send another ACK to satisfy Sender
	return 1;
}

int main(int argc, char* argv[])
{
	assert(argc == 3 || argc == 4); // Assert that we have the correct number of arguments
	isLogging = (argc == 3) ? 0 : 1; // If we have a third argument that means log file

	int port = atoi(argv[1]); // The port that we will listen on
	char* out_file_name = argv[2]; // The file to write our data to
	char* log_file = NULL; if(isLogging) { log_file = argv[3]; } // The file to log to

	fptr = fopen(out_file_name, "w"); // Open the file for writing
	assert(fptr); // Assert we can write to it

	if(isLogging) { write_log = fopen(log_file, "w"); assert(write_log); } // If we're logging open the log file

	int recvrSocket = socket(AF_INET, SOCK_DGRAM, 0); // Make a socket that we can receive packets to (AF_INET for IPv4 communication domain, SOCK_DGRAM for UDP, 0 is automatic protocol)

	struct sockaddr_in server_addr = {0}; // Bind our socket to this hosts network interfaces and listen on the specified port
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port); // Bind on this port (network order)
	server_addr.sin_addr.s_addr = INADDR_ANY; // Bind on any network interface

	assert((bind(recvrSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr))) != -1); // Assert that we binded
	
	init_random(); // Seed the random values for unreliable sending/receiving later (if used)
	uint8_t responseBuf[1472] = {0}; // Where we will store our packets that we have received

	while(1) { // If the recv times out, then this loop will break
		//recv_packet(recvrSocket, responseBuf, 1472, &senderAddr, &addrLen); // For unreliable receiving. Check UnreliableChannel
		recvfrom(recvrSocket, responseBuf, 1472, 0, &senderAddr, &addrLen);
		unsigned int calc_checksum = 0;
		int the_check = checkChecksum(responseBuf, &calc_checksum);
		if (isLogging) {
			fprintf(write_log, "Packet received; "); printPack(responseBuf); fprintf(write_log, "checksum_calculated=%x; ", calc_checksum);
			fprintf(write_log, "status="); the_check ? fprintf(write_log, "NOT_CORRUPT\n\n") : fprintf(write_log, "CORRUPT\n\n");
		}
		if (the_check) { // Check the checksum of the packet and see if it hasn't been corrupted
			if(!checkSeq(recvrSocket, responseBuf)) { printf("Successfully received all packets\n"); break; }
		} // If it has, don't do anything, and wait for the Sender to timeout and resend
	}

	fclose(fptr); // Close our file that we're writing to
	if (isLogging) { fclose(write_log); }
	
	return 0;
}
