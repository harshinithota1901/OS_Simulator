CC=gcc
CFLAGS=-Wall -ggdb

default: oss user

shared.o: shared.c master.h
	$(CC) $(CFLAGS) -c shared.c

oss: oss.c master.h
	$(CC) $(CFLAGS) oss.c -o oss

user: user.c master.h
	$(CC) $(CFLAGS) user.c -o user

clean:
	rm -f oss user
