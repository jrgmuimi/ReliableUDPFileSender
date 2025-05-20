# ReliableUDPFileSender
A UNIX file transfer program that can reliably send a file from one host to another over via UDP sockets

THe reliable transfer works by supplying a custom header, along with the message, in the payload of the UDP datagram. The header includes 4 unsigned integer fields: The type (TYPE) of message (DATA, ACK), the sequence number (SEQNUM) of the message, the length (LEN) (in bytes) of the message + bytes of the header, and finally a CRC32 checksum that is calculated using the TYPE, SEQNUM, LEN (for ACK messages) or TYPE, SEQNUM, LEN, and DATA (for DATA messages).

The Sender divides the bytes of the target file in chunks of 1456 bytes. An additional 16 bytes for the custom header, 8 bytes for the UDP header, and 20 bytes for the IP header total a maximum of 1500 bytes per packet.

The Sender sequentially sends packets to the Receiver: The Sender will NOT send the next packet if it has NOT received an ACK for the last packet sent. Because the Receiver ignores corrupt packets
