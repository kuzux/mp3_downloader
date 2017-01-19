CFLAGS?=-O2 -g -Wall -Werror
OBJS:=main.o
LIBS:=-ledit -lsqlite3
BINS:=client server

all: server client

.PHONY: all clean

server: server.c
	gcc $< -o $@ $(CFLAGS) $(LIBS)

client: client.c
	gcc $< -o $@ $(CFLAGS) $(LIBS)

clean:
	rm *.o $(BINS)