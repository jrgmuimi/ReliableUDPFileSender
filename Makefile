CC = gcc
TARG1 = Sender
TARG2 = Receiver
EXTRA = UnreliableChannel.c
CFLAGS = -g -pthread -Wall -std=c99 -pedantic-errors

all: $(TARG1) $(TARG2)

$(TARG1) : $(TARG1).c $(EXTRA)
	$(CC) $(TARG1).c $(EXTRA) -o $(TARG1) $(CFLAGS)

$(TARG2) : $(TARG2).c $(EXTRA)
	$(CC) $(TARG2).c $(EXTRA) -o $(TARG2) $(CFLAGS)

clean:
	rm -f $(TARG1) $(TARG2) *.txt
