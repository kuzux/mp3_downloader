all: server client

server: server.c
	gcc server.c -o server -lsqlite3 

client: client.c
	gcc client.c -o client

clean:
	rm client
	rm server
