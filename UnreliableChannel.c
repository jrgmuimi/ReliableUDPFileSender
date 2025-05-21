#include <arpa/inet.h>
#include <stdlib.h>
#include <time.h>

static double probability = 0.8; // Edit here to change rate of channel errors

void init_random() { srand(time(NULL)); } // Seed random

ssize_t recv_packet(int socket, uint8_t* buffer, size_t length, struct sockaddr* senderAddr, socklen_t* addrLen)
{
	ssize_t bytes = recvfrom(socket, buffer, length, 0, senderAddr, addrLen);
	if (bytes>0) {  // +1 is so we don't include 1.0. If we want to turn off probability then we set to 1.0 since shamiko will never be 1
		double shamiko = (double)rand() / (RAND_MAX+1.0);
		if (shamiko > probability) { // %40 of the time corrupt everything in the message
			for (int i=0;i<length;i++) { buffer[i] += 1; }
		}	// Let's see what this does
	}
	return bytes;
}


ssize_t send_packet(int socket, const void* message, size_t length, const struct sockaddr* dest_addr, socklen_t dest_len)
{
	ssize_t bytes = -1; // On sendto error, -1 is returned
	double momo = (double)rand() / (RAND_MAX+1.0);
	if (momo < probability) { bytes = sendto(socket, message, length, 0, dest_addr, dest_len); } // %60 of the time actually send the packet, the other 40 don't even send
	return bytes;
}
