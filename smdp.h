#ifndef _SMDP_H
#define _SMDP_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* respond with the same message
   for testing purposes */
#define SMDP_ECHO 0

/* request a listing
   or respond with a number of rows */
#define SMDP_LIST 1

/* request authentication */
#define SMDP_USER 2

#define SMDP_PASS 3

/* authentication accepted */
#define SMDP_ACCEPT 4

/* authentication denied */
#define SMDP_DENY 5

/* each row in a list response */
#define SMDP_ROW 6

/* request file
   or respond with file */
#define SMDP_FILE 7 

/* request random file */
#define SMDP_RANDOM 8

/* no file with given id */
#define SMDP_NOFILE 9

/* send file from server to client
   with filename */
#define SMDP_UPLOAD 10

/* sent from client
   to indicate a closed connection */
#define SMDP_CLOSE 11

void error(char* msg){
    perror(msg);
    exit(1);
}

uint32_t smdp_read_int(int sock){
    uint32_t tmp;
    int n = read(sock, &tmp, sizeof(tmp));

    if(n < 0){
        error("Error reading from socket");
    }

    return tmp;
}

int smdp_read_str(int sock, char* buf, int buflen){
    int len = smdp_read_int(sock);

    if(len >= buflen){
        fprintf(stderr, "Possible buffer overflow\n");
    }

    int n = read(sock, buf, len);
    buf[len] = 0;

    if(n < 0){
        error("Error reading from socket");
    }

    return n;
}

void smdp_write_int(int sock, uint32_t type){
    int n = write(sock, &type, sizeof(type));
    if(n < 0){
        error("Error writing to socket");
    }
}

void smdp_write_str(int sock, char* str){
    uint32_t len = strlen(str);
    int n = write(sock, &len, sizeof(len));
    if(n < 0){
        error("Error writing to socket");
    }
    write(sock, str, len);
    if(n < 0){
        error("Error writing to socket");
    }
}

#endif
