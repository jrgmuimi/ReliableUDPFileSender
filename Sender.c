#include <stdio.h> // fprintf
#include <assert.h> // For assertions
#include <arpa/inet.h> // For socket/network operations
#include <stdlib.h> // Calloc
#include <string.h> // Fpr strtok and other str functions such as memcpy
#include <poll.h> // Used to set a timer for a response within 500ms (if no reponse with 500ms then likely lost)
#include "UnreliableChannel.h" // For unreliable sending/receiving. See the associated C and Header file.
// Make sure to replace the randomization when finally cleaning up. Remove print statements

#define MAX_DATA (1456) // Maximum amount of bytes (excluding header) a MTP message can contain
#define HEADER_BYTES (16) // Amount of header bytes

static unsigned int num_packs = 0; // Number of packets the target file is split into
static uint8_t** packet_list = NULL; // Pointer list that stores pointers to each packet
static struct sockaddr_in dest_addr = {0}; // The destination address we want to send to

static FILE* writeFile = NULL;
static int isLogging = 0;
static int attempts = 0;

static unsigned int parseIP(char* recvrIP)
{
	char* token = strtok(recvrIP, "."); // To retrieve the numbers separated by the periods we tokenize
	unsigned int totality = 0; // The value of the IP without the periods. We will add each segment here
	
	int shift = 24; // Amount to shift the tokenized values over to depending on its location
	while(token) // While we can tokenize (periods left)
	{
		unsigned int segment = atoi(token); // Convert the segment to an int
		segment<<=shift; // If it's the beginning shift it over 24 bits (8 bits per segment) and decrease by 8 each time
		totality+=segment; // Add to totality
		shift-=8; // Shift next 8 bits
		token = strtok(NULL, "."); // Get next token
	}

	return totality;
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

	if (isLogging) { fprintf(writeFile, "type=%s; seqNum=%u; length=%u; checksum=%x\n\n", typeStr, ntohl(seqNum), ntohl(length), ntohl(checksum)); }
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

static unsigned int packetsToSend(char *in_file_name)
{ // Gets number of packets to send
	FILE* fptr = fopen(in_file_name, "r"); 
	assert(fptr); // Assert that we can open the file for reading

	char buffer[MAX_DATA] = {0}; // The maximum amount of data a MTP message can contain is 1456 bytes because the header is 16 bytes
	unsigned int counter = 0; // Count number of packs

	while (fread(buffer, sizeof(char), MAX_DATA, fptr)) { counter+=1; } // While we can keep reading 1456 bytes keep incrementing the counter

	fclose(fptr); // Close our file
	return counter; // Return number of packets
}

static void init_packets(char *in_file_name)
{
	FILE* fptr = fopen(in_file_name, "r");
	assert(fptr);

	char buffer[MAX_DATA] = {0}; // Make a buffer to read the data in a while loop using fread
	unsigned int counter = 0; // Counter is for the packet/seq number when sending 
	
	size_t bytes = 0; // The amount of bytes read. If there are no bytes to read then this will be zero
	for(int i = 0; i < num_packs; i++)
	{
		int callFinality = ((i+1) == num_packs); // The last packet has descended
		if(!callFinality) { bytes = fread(buffer, sizeof(char), MAX_DATA, fptr); }
		uint32_t length = (callFinality) ? 16 : bytes+HEADER_BYTES; // The max amount of data we're reading is 1456, plus we need header (16 bytes)

		packet_list[counter] = calloc(length, sizeof(uint8_t)); // Make the packet with 1472 bytes

		uint8_t* message = calloc(length-4, sizeof(uint8_t)); // We need this for the checksum (first 3 headers + actual message/data)
		assert(message); // A checksum is 4 bytes, so only exlude checksum (-4)
			
		uint8_t* my_packet = packet_list[counter]; // Assign a variable to the packet to make it easier to access and use less chars
		assert(my_packet); // Assert we calloced correctly earlier

		uint32_t type = (callFinality) ? 2 : 1; // The type of the packet we're sending (1 is DATA, 0 is ACK, 2 is FIN)
		uint32_t seqNum = counter; // Sequence number of packet from counter

		memcpy(message, &type, sizeof(uint32_t)); // Copy the type to the message for checksum
		memcpy(message+4, &seqNum, sizeof(uint32_t)); // Copy the seqNum to the message for checksum
		memcpy(message+8, &length, sizeof(uint32_t)); // Copy the length to the message for checksum
		if (!callFinality) { memcpy(message+12, buffer, bytes); } // Copy the actual data to the message for checksum

		uint32_t checksum = crc32(message, length-4);

		type = htonl(type); // Convert these to network byte order since they're numbers and endianess could affect them
		seqNum = htonl(seqNum); // We don't need to convert the data cause they're just characters
		length = htonl(length);
		checksum = htonl(checksum);
		
		memcpy(my_packet, &type, sizeof(uint32_t));
		memcpy(my_packet+4, &seqNum, sizeof(uint32_t));
		memcpy(my_packet+8, &length, sizeof(uint32_t));
		memcpy(my_packet+12, &checksum, sizeof(uint32_t)); // First 16 bytes is the Header data
		if (!callFinality) { memcpy(my_packet+16, buffer, bytes); } // Finally we copy the data
		
		counter+=1; // Increase the counter

		free(message); // We don't need the checksum bytes anymore since we already calculated
	}
	
	fclose(fptr);
}

static void generic_send(int socket, int i)
{
	size_t length = 0; memcpy(&length, packet_list[i]+8, sizeof(uint32_t)); length = ntohl(length); // Get the length of the pack quickly so we can call sendto
	if (isLogging) { fprintf(writeFile, "Packet sent; "); printPack(packet_list[i]); }
	//send_packet(socket, packet_list[i], length, (struct sockaddr*)&dest_addr,(socklen_t)sizeof(dest_addr)); // For unreliable sending. Check UnreliableChannel
	sendto(socket, packet_list[i], length, 0, (struct sockaddr*)&dest_addr, (socklen_t)sizeof(dest_addr)); // 0 flags
}

static int checkChecksum(uint8_t* buffer, unsigned int* storeCalc)
{ // Check the checksum sent in the ACK packet. This check is not necessary for reasons listed in receiveThis(), but its good for logging purposes
	uint8_t* data = calloc(12, sizeof(uint8_t));

	uint32_t type = 0;
	uint32_t seqNum = 0;
	uint32_t length = 0;
	uint32_t checksum = 0;

	memcpy(&type, buffer, sizeof(uint32_t));
	memcpy(&seqNum, buffer+4, sizeof(uint32_t));
	memcpy(&length, buffer+8, sizeof(uint32_t));
	memcpy(&checksum, buffer+12, sizeof(uint32_t));

	type = ntohl(type);
	seqNum = ntohl(seqNum);
	length = ntohl(length);

	memcpy(data, &type, sizeof(uint32_t));
	memcpy(data+4, &seqNum, sizeof(uint32_t));
	memcpy(data+8, &length, sizeof(uint32_t));

	unsigned int crc = crc32(data, 12);
	*storeCalc = crc; // ntohl not required
	free(data);
	return (crc == ntohl(checksum));
}

static int receiveThis(int socket, int seq)
{
	struct pollfd fds;
	fds.fd = socket;
	fds.events = POLLIN; // Make a poll to check if data has arrived to the socket within 500ms of sending
	int activity;
	
	reset_poll:
	activity = poll(&fds, 1, 500);  // If no reponse within 500ms, send the packet again and wait again for a reply.
	if (activity <= 0) { 
		// Amount of times we've attempted sending a packet and gotten no response
		if (attempts == 5) { printf("No response after 5 attempts\n"); return 0; }
		attempts+=1;
		if (isLogging) { fprintf(writeFile, "Timeout for packet seqNum=%u... Resending\n\n", seq); }
		generic_send(socket, seq);
		goto reset_poll;
	}
	else // If the destination host is not receiving then this will loop forever
	{ 
		// We don't care about checksums because the Receiver ignores invalid checksums, 
		// and so if the Receiver sends a message to us, it must have definitely received the packet. Also we force sending in order.
		// So an ACK for a different sequence number other than one we're expecting can't arrive.
		attempts = 0; // Amount of times we've attempted sending a packet and gotten no response
		uint8_t responseBuf[1472] = {0};
		//recv_packet(socket, responseBuf, 1472, NULL, NULL); // For unreliable receiving. See Unreliable Channel.
		recvfrom(socket, responseBuf, 1472, 0, NULL, NULL);

		unsigned int checksum_calc = 0;
		int result = checkChecksum(responseBuf, &checksum_calc);
		if (isLogging) { 
			fprintf(writeFile, "Packet received; "); printPack(responseBuf); fprintf(writeFile, "checksum_calculated=%x; ", checksum_calc);
			fprintf(writeFile, "status="); result ? fprintf(writeFile, "NOT_CORRUPT\n\n") : fprintf(writeFile, "CORRUPT\n\n");
		}
	}
	return 1;
}

int main(int argc, char* argv[])
{
	assert(argc == 4 || argc == 5); // Assert that we have the correct number of arguments
	isLogging = (argc == 4) ? 0 : 1;

	char* recvrIP = argv[1]; // The IP that will receive the packets we send
	unsigned int parsedIP = parseIP(recvrIP); // Parse the IP into the proper format, an unsigned int, without the periods
	int recvrPort = atoi(argv[2]); // Convert the port to a number, will be truncated later (unsigned short)
	char* in_file_name = argv[3]; // Get the file name to read from
	char* log_file = NULL; if(isLogging) { log_file = argv[4]; }

	num_packs = packetsToSend(in_file_name); // Get the number of packets/segments the file can be divided itno
	assert(num_packs);
	num_packs+=1; // +1 for FIN packet

	packet_list = calloc(num_packs, sizeof(uint8_t*)); // Make a list of pointers for each packet (pointer). +1 for FINALITY
	assert(packet_list); // Assert that it actually exists
	init_packets(in_file_name); // Finally load the packets with the data

	int senderSocket = socket(AF_INET, SOCK_DGRAM, 0); // Make the sender socket (AF_INET for IPv4 communication domain, SOCK_DGRAM for UDP, 0 is automatic protocol)

	dest_addr.sin_family = AF_INET; // For the sockaddr_in
	dest_addr.sin_port = htons(recvrPort); // The port we will send to (convert to network order)
	dest_addr.sin_addr.s_addr = htonl(parsedIP); // The IP we will send to (convert to network order)

	init_random(); // Seed the random values for unreliable sending/receiving later (if used)
	if (isLogging) { writeFile = fopen(log_file, "w"); assert(writeFile); } // Open log file for writing

	unsigned int i;
	for(i = 0; i < num_packs; i++) // Iterate over all packets in the packet list
	{
		generic_send(senderSocket, i); // Generically send it using the seq number
		if(!receiveThis(senderSocket, i)) { break; } // Finally we will force a reply from the server
	}

	char* response = ((i+1)>=num_packs) ? "Sender has successfully sent all packets\n" : "Sender has not sent all packets\n";
	printf(response);
	if (isLogging) { fclose(writeFile); }

	return 0;
}
