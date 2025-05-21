# ReliableUDPFileSender
A UNIX file transfer program that can reliably send a file from one host to another over via UDP sockets

The reliable transfer works by supplying a custom header, along with the message, in the payload of the UDP datagram. The header includes 4 unsigned integer fields: The type (TYPE) of message (DATA, ACK, FIN), the sequence number (SEQNUM) of the message, the length (LEN) (in bytes) of the message + bytes of the header, and finally a CRC32 checksum that is calculated using the TYPE, SEQNUM, LEN (for ACK, FIN messages) or TYPE, SEQNUM, LEN, and DATA (for DATA messages).

The Sender divides the bytes of the target file in chunks of 1456 bytes. An additional 16 bytes for the custom header, 8 bytes for the UDP header, and 20 bytes for the IP header total a maximum of 1500 bytes per packet.

The Sender sequentially sends packets to the Receiver: The Sender will NOT send the next packet if it has NOT received an ACK for the last packet sent. If there has been no response (ACK) from the Receiver after 500ms, the Sender will timeout and resend the last sent packet. The Sender does not care if the ACK received is corrupted (calculated checksum and included checksum in the packet do not match) or not because the Receiver ignores all corrupt packets and will NOT send an ACK for a corrupt packet. Hence, if the Sender receives an ACK, we can say with confidence that the Receiver correctly received the message.

The Sender will timeout and exit after 5 attempts of not receiving a response (500ms * 5 = 2.5 seconds): This indicates that either the Receiver is offline, on a different port, or 5 consecutive packets were corrupt/lost. The Receiver does not have a timeout restriction and will not exit unless the user stops the program, or it has finished receiving all of the packets. Hence, it is recommended to start the Receiver first and then the Sender.

Usage:  
./Sender [receiver-IP] [receiver-port] [input-file] [sender-log-file (optional)]  
./Receiver [receiver-port] [output-file] [receiver-log-file (optional)]


