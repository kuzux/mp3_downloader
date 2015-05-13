#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sqlite3.h>
#include <getopt.h>
#include "smdp.h"

#define DEFAULT_PORT 3535

char buf[1024];
sqlite3* db;

char username[256];
char password[256];
int authenticated = 0;

int listenfd;
int connfd;

int verbose = 0;
int port_no = DEFAULT_PORT;

const struct option long_options[] = {
    {"verbose", no_argument, 0, 'v'},
    {"port", required_argument, 0, 'p'},
    {0, 0, 0, 0}
};

void dberror(char* msg){
    fprintf(stderr, "%s: %s\n", msg, sqlite3_errmsg(db));
    sqlite3_close(db);
    exit(1);
}

void open_db(){

    if(verbose){
        printf("Opening db\n");
    }

    /* open a database connection and check for errors */
    int rc;

    rc = sqlite3_open("server.db", &db);
    if(rc != SQLITE_OK){
        dberror("Cannot open database");
    }

    /* create the tables if they don't exist */
    char* schema = "CREATE TABLE IF NOT EXISTS users(username TEXT PRIMARY KEY, password TEXT);"
                   "CREATE TABLE IF NOT EXISTS files(mid INTEGER PRIMARY KEY ASC, name TEXT, path TEXT);";

    char* err_msg = NULL;
    rc = sqlite3_exec(db, schema, 0, 0, &err_msg);
    
    if(rc != SQLITE_OK){
        dberror(err_msg);

        sqlite3_free(err_msg);
    }


    if(verbose){
        printf("db opened\n");
    }


}

void do_echo(int sock){
    /* clear buffer and read a string */
    memset(buf, 0, 1024);
    smdp_read_str(sock, buf, 1024);

    /* write an echo command and write the string back */
    smdp_write_int(sock, SMDP_ECHO);
    smdp_write_str(sock, buf);
}

int list_callback(void* arg0, int argc, char** argv, char** colNames){

    /* this callback is passed to sqlite3_exec as an argument
       and socket handle is passed as an argument to it as well */
    int sock = *(int*)arg0;

    if(argc < 3){
        fprintf(stderr, "Too few columns returned, (shouldn't happen unless database is corrupt)");
        exit(1);
    }

    /* write the row command and three columns as strings (mid, name and path) */
    smdp_write_int(sock, SMDP_ROW);
    smdp_write_str(sock, argv[0]);
    smdp_write_str(sock, argv[1]);
    smdp_write_str(sock, argv[2]);

    /* needs to return 0 for sqlite3_exec to continue with other rows */
    return 0;
}

void do_list(int sock){

    if(verbose){
        printf("Handling list operation\n");
    }

    char* sql = "SELECT * FROM files";
    char* csql = "SELECT COUNT(*) FROM files";
    char* err_msg = NULL;
    sqlite3_stmt* stmt;
    int rc;

    /* we need to find the number of rows to send */
    rc = sqlite3_prepare_v2(db, csql, -1, &stmt, 0);
    if(rc != SQLITE_OK){
        dberror("Failed to fetch data");
    }

    rc = sqlite3_step(stmt);

    if(rc != SQLITE_ROW){
        dberror("Failed to fetch data");
    }

    int len = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);

    /* send the command and number of rows */
    smdp_write_int(sock, SMDP_LIST);
    smdp_write_int(sock, len);

    /* send the rows themselves (via list_callback function) */
    rc = sqlite3_exec(db, sql, list_callback, &sock, &err_msg);

    if(rc != SQLITE_OK){
        dberror(err_msg);

        sqlite3_free(err_msg);
    }
}

void do_user(int sock){
    /* just read a string from socket and write it to username buffer
       no authentication done here */
    memset(username, 0, 256);
    smdp_read_str(sock, username, 256);

    if(verbose){
        printf("%s logging in\n", username);
    }
}

void do_pass(int sock){
    if(verbose){
        printf("%s sent password\n", username);
    }

    /* read the password from socket connection into password buffer */
    memset(password, 0, 256);  
    smdp_read_str(sock, password, 256);

    /* fetch username and password from database */
    char* sql = "SELECT * FROM users WHERE username = ?";

    sqlite3_stmt* stmt;
    int rc;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if(rc != SQLITE_OK){
        dberror("Failed to execute statement");
    }

    sqlite3_bind_text(stmt, 1, username, strlen(username), SQLITE_STATIC);

    rc = sqlite3_step(stmt);


    if(rc == SQLITE_ROW){
        /* a user with that username exists */
        const char* realuser = sqlite3_column_text(stmt, 0);
        const char* realpass = sqlite3_column_text(stmt, 1);

        /* check if password matches 
           (we really shouldn't be doing that - storing passwords in plaintext in db)
           (but on the other hand, we really shouldn't be sending passwords 
           in plaintext over network either) */

        if(strcmp(password, realpass)==0){
            authenticated = 1;
        } else {
            authenticated = 0;
        }
    } else {
        /* no user with the specified username, deny authentication */
        authenticated = 0;
    }

    /* inform the client if authentication was successful or not */
    if(authenticated){
        if(verbose){
            printf("%s successfully logged in\n", username);
        }

        smdp_write_int(sock, SMDP_ACCEPT);
    } else {

        if(verbose){
            printf("Authentication failed for %s\n", username);
        }

        smdp_write_int(sock, SMDP_DENY);
    }
}

/* reads a file from given path and sends it through the socket connection
   by reading the file into the buffer */
void send_file(int sock, const char* path){

    if(verbose){
        printf("Sending file %s\n", path);
    }


    uint32_t len;
    struct stat st;

    /* stat returns -1 when the given file does not exist */
    if(stat(path, &st) < 0){
        /* so in that case just send nofile, but that shouldn't happen
           unless a non-existent path is in the database */
        smdp_write_int(sock, SMDP_NOFILE);
        fprintf(stderr, "File not found: %s\n", path);
        return;
    }

    /* send the file command and file size */
    len = st.st_size;

    smdp_write_int(sock, SMDP_FILE);
    smdp_write_int(sock, len);

    /* open the file */
    FILE* fp;

    fp = fopen(path, "rb");
    
    uint32_t counter = 0;

    /* read into the buffer and send part by part until end of file */
    while(counter < len){
        uint32_t to_read = (len-counter<1024)?(len-counter):1024;
        memset(buf, 0, 1024);
        fread(buf, sizeof(char), to_read, fp);

        int n = write(sock, buf, to_read);
        
        if(n < 0){
            error("Error writing to socket");
        }

        counter += to_read;
    }

    fclose(fp);
}

void do_file(int sock){

    int mid = smdp_read_int(sock);

    if(verbose){
        printf("Handling file command for id %d\n", mid);
    }


    /* reject if the client isn't authenticated */
    if(!authenticated){
        smdp_write_int(sock, SMDP_DENY);
        return;
    }

    /* get the requested media id from socket and fetch it from the database */
    char* sql = "SELECT path FROM files WHERE mid=?";

    sqlite3_stmt* stmt;
    int rc;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if(rc != SQLITE_OK){
        dberror("Failed to execute statement");
    }

    sqlite3_bind_int(stmt, 1, mid);

    rc = sqlite3_step(stmt);

    if(rc == SQLITE_ROW){
        /* found the file, send it */
        const char* path = sqlite3_column_text(stmt, 0);
        send_file(sock, path);
    } else {

        if(verbose){
            printf("File with id %d not found\n", mid);
        }

        /* no file with that id exists */
        smdp_write_int(sock, SMDP_NOFILE);
    }
}

void do_random(int sock){

    if(verbose){
        printf("Handling random file command\n");
    }

    /* reject if the client isn't authenticated */
    if(!authenticated){
        smdp_write_int(sock, SMDP_DENY);
        return;
    }

    /* get a random row from database */
    char* sql = "SELECT mid, path FROM files ORDER BY RANDOM() LIMIT 1";

    sqlite3_stmt* stmt;
    int rc;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if(rc != SQLITE_OK){
        dberror("Failed to execute statement");
    }

    rc = sqlite3_step(stmt);


    if(rc == SQLITE_ROW){
        /* found one, send its id and then send the file */
        int mid = sqlite3_column_int(stmt, 0);
        const char* path = sqlite3_column_text(stmt, 1);

        printf("%d %s\n", mid, path);

        smdp_write_int(sock, mid);
        send_file(sock, path);
    } else {
        /* apparently, there are no rows in the table */
        fprintf(stderr, "There are no files in the database, really?\n");
        smdp_write_int(sock, SMDP_NOFILE);
    }
}

void handle(int sock){
    int n = 0;
    int rc = 0;

    open_db();

    /* we'll be using select system call for implementing timeout
       fd_set is a bit-set type of data structure for specifying sockets
       to listen to while timeval is used for the timeout functionality */
    struct timeval timeout, tv;
    fd_set master, readfds;


    /* since select system call modifies the fd_set and timeval arguments
       it receives, we need to set two copies from each, one master for keeping
       the initial value safe, and another for passing to the system call */
    timeout.tv_sec = 120;
    timeout.tv_usec = 0;

    FD_ZERO(&master);
    FD_ZERO(&readfds);

    FD_SET(sock, &master);

    /* we need to handle messages in an infinite loop since the protocol 
       is designed for keeping a connection open for a session and issuing 
       commands that alter the state (a la FTP) */
    for(;;){
        /* copy the structures for select call */
        memcpy(&readfds, &master, sizeof(master));
        memcpy(&tv, &timeout, sizeof(timeout));

        /* select (oddly) needs the maximum descriptor id + 1, three fd_sets for read, write 
           and exceptions and the timeout option */
        int res = select(sock + 1, &readfds, NULL, NULL, &tv);

        /* select returns -1 for error and 0 for timeout, something positive if it is read
           so we can resume after checking for these conditions */
        if(res < 0){
            error("select() failed");
        } else if(res == 0){
            fprintf(stderr, "Connection timed out\n");
            break;
        }

        /* read the message type */
        uint32_t msgtype = smdp_read_int(sock);

        /* connection is officially closed, goodbye */
        if(msgtype==SMDP_CLOSE){
            if(verbose){
                printf("Closing connection\n");
            }

            break;  
        } 

        /* dispatch the message based on its message type
           the dispatched functions read additional data from the socket
           as it is necessary */
        switch(msgtype){
            case SMDP_ECHO:
            do_echo(sock);
            break;

            case SMDP_LIST:
            do_list(sock);
            break;

            case SMDP_USER:
            do_user(sock);
            break;

            case SMDP_PASS:
            do_pass(sock);
            break;

            case SMDP_FILE:
            do_file(sock);
            break;

            case SMDP_RANDOM:
            do_random(sock);
            break;

            default:
            fprintf(stderr, "Invalid message type %d\n", msgtype);
            goto _handle_end;
            break;
        }
    
    }

    _handle_end:
    sqlite3_close(db);
}

void setup(){
    if(verbose){
        printf("Opening Sockets\n");
    }

    listenfd = 0;
    connfd = 0;

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    /* use a tcp socket */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    if(listenfd < 0){
        error("Error opening socket");
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_no);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* bind the address into socket */
    if(bind(listenfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0){
        error("Error binding socket");
    }

    /* listen for connections with a backlog of 5, 
       hopefully that won't fill up since we're using a forking server
       (also hopefully, anon won't be angry at us and won't DDOS us) */
    listen(listenfd, 5);

    if(verbose){
        printf("Listening\n");
    }
}

void parse_opts(int argc, char** argv){
    int c;

    for(;;){
        int option_index = 0;

        c = getopt_long(argc, argv, "p:v", long_options, &option_index);

        if(c == -1) break;

        switch(c){
            case 'p':
            port_no = atoi(optarg);
            break;

            case 'v':
            verbose = 1;
            printf("Verbose mode\n");
            break;

            default:
            abort();
        }
    }
}

int main(int argc, char** argv){
    parse_opts(argc, argv);
    setup();

    for(;;){
        /* wait until a new connection is made
           last two arguments are null since we don't care about
           the connecting client's address
           (we might want to change that if we add logging support and stuff) */
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);


        if(connfd < 0){
            error("Error establishing connection");
        }

        if(verbose){
            printf("Accepting new connection\n");
        }

        int pid = fork();

        if(pid < 0){
            error("Error on fork");
        }

        if(pid == 0){
            /* child process handling the connection */
            close(listenfd);
            handle(connfd);
            close(connfd);
            return 0;
        } else {
            /* parent process continuing the loop */
            close(connfd);
        }
    }
    
    return 0;
}
