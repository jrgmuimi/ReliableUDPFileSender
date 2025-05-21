#ifndef UNRELIABLE_H
#define UNRELIABLE_H
// Simulates packet loss and packet corruption
void init_random();
ssize_t recv_packet(int, uint8_t*, size_t, struct sockaddr*, socklen_t*);
ssize_t send_packet(int, const void*, size_t, const struct sockaddr*, socklen_t);

#endif
