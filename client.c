#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <getopt.h>
#include <signal.h>
#include <histedit.h>
#include "smdp.h"

#define DEFAULT_PORT 3535

char buf[1024];
char* line;

int sockfd;

int verbose = 0;
int port_no = DEFAULT_PORT;

const struct option long_options[] = {
    {"verbose", no_argument, 0, 'v'},
    {"port", required_argument, 0, 'p'},
    {0, 0, 0, 0}
};

int running = 1;

void do_echo(){
    fgets(buf, 1023, stdin);
    smdp_write_int(sockfd, SMDP_ECHO);
    smdp_write_str(sockfd, buf);

    memset(buf, 0, 1024);
    smdp_read_int(sockfd); // ignore type, assume to be echo
    smdp_read_str(sockfd, buf, 1024);

    printf("%s\n", buf);    
}

void do_list(){
    smdp_write_int(sockfd, SMDP_LIST);

    smdp_read_int(sockfd); // ignore type, assume to be list
    int rows = smdp_read_int(sockfd);

    int i;
    for(i=0;i<rows;i++){
        smdp_read_int(sockfd); // ignore type, assume to be row
        
        memset(buf, 0, 1024);
        smdp_read_str(sockfd, buf, 1024);
        printf("%s ", buf);
        
        memset(buf, 0, 1024);
        smdp_read_str(sockfd, buf, 1024);
        printf("%s ", buf);

        memset(buf, 0, 1024);
        smdp_read_str(sockfd, buf, 1024);
        printf("%s\n", buf);
    }
}

void do_user(char* username){
    smdp_write_int(sockfd, SMDP_USER);
    smdp_write_str(sockfd, username);
}

void do_pass(char* password){
    smdp_write_int(sockfd, SMDP_PASS);
    smdp_write_str(sockfd, password);

    uint32_t type = smdp_read_int(sockfd);

    if(type == SMDP_ACCEPT){
        printf("Successfully logged in\n");
    } else {
        printf("Invalid username/password\n");
    }
}

/* reads a file from given path and sends it through the socket connection
   by reading the file into the buffer */
void send_file(const char* path){

    if(verbose){
        printf("Sending file %s\n", path);
    }

    uint32_t len;
    struct stat st;

    /* stat returns -1 when the given file does not exist */
    if(stat(path, &st) < 0){
        fprintf(stderr, "File not found: %s\n", path);
        return;
    }

    /* send the file command and file size */
    len = st.st_size;

    smdp_write_int(sockfd, len);

    /* open the file */
    FILE* fp;

    fp = fopen(path, "rb");
    
    uint32_t counter = 0;

    /* read into the buffer and send part by part until end of file */
    while(counter < len){
        uint32_t to_read = (len-counter<1024)?(len-counter):1024;
        memset(buf, 0, 1024);
        fread(buf, sizeof(char), to_read, fp);

        int n = write(sockfd, buf, to_read);
        
        if(n < 0){
            error("Error writing to socket");
        }

        counter += to_read;
    }

    fclose(fp);
}

void receive_file(char* path){
    FILE* fp;

    fp = fopen(path, "wb");
    
    uint32_t len = smdp_read_int(sockfd);
    uint32_t counter = 0;

    /* read into the buffer and send part by part until end of file */
    while(counter < len){
        uint32_t to_read = (len-counter<1024)?(len-counter):1024;
        int n = read(sockfd, buf, to_read);

        if(n < 0){
            error("Error reading from socket");
        }

        fwrite(buf, sizeof(char), to_read, fp);
        
        counter += to_read;
    }


    fclose(fp);
}

void do_download(int mid, char* path){
    smdp_write_int(sockfd, SMDP_FILE);
    smdp_write_int(sockfd, mid);

    int resp = smdp_read_int(sockfd);

    if(resp == SMDP_DENY){
        printf("Access denied\n");
    } else if(resp == SMDP_NOFILE){
        printf("No such file\n");
    } else {
        receive_file(path);
    }
}

void do_random(char* path){
    smdp_write_int(sockfd, SMDP_RANDOM);

    int resp = smdp_read_int(sockfd);
    if(resp == SMDP_DENY){
        printf("Access denied\n");
    } else if(resp == SMDP_NOFILE){
        printf("No such file\n");
    } else {
        receive_file(path);
    }
}

void do_upload(char* name, char* path){
    struct stat st;

    /* stat returns -1 when the given file does not exist */
    if(stat(path, &st) < 0){
        fprintf(stderr, "File not found: %s\n", path);
        return;
    }

    smdp_write_int(sockfd, SMDP_UPLOAD);

    int res = smdp_read_int(sockfd);

    if(res == SMDP_DENY){
        printf("Access denied\n");
        return;
    }

    smdp_write_str(sockfd, name);

    send_file(path);
}

void signal_handler(int sig){
    smdp_write_int(sockfd, SMDP_CLOSE);
    close(sockfd);
}

void setup(char* hostname){
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGSEGV, signal_handler);

    sockfd = 0;

    struct sockaddr_in server_addr;
    struct hostent* server;

    memset(&server_addr, 0, sizeof(server_addr));
    memset(buf, 0, 1024);

    server = gethostbyname(hostname);

    if(server == NULL){
        printf("ERROR: No such host\n");
        exit(0);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_no);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        error("Error creating socket");
    }

    if(connect(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0){
        error("Error connecting to server");
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

void help_message(){
    printf("Commands: \n");
    printf("* list \n");
    printf("* user <username> \n");
    printf("* pass <password> \n");
    printf("* download <mid> <filename> \n");
    printf("* random <filename>\n");
    printf("* upload <name> <path>\n");
    printf("* exit\n");
}

void do_command(char* command){
    char* tok;

    tok = strtok(command, " \n");

    if(strcmp(tok, "list")==0){
        do_list();
    } else if(strcmp(tok, "user")==0){
        tok = strtok(NULL, " \n");
        do_user(tok);
    } else if(strcmp(tok, "pass")==0){
        tok = strtok(NULL, " \n");
        do_pass(tok);
    } else if(strcmp(tok, "download")==0){
        tok = strtok(NULL, " \n");
        int mid = atoi(tok);
        tok = strtok(NULL, " \n");
        do_download(mid, tok);
    } else if(strcmp(tok, "random")==0){
        tok = strtok(NULL, " \n");
        do_random(tok);
    } else if(strcmp(tok, "upload")==0){
        tok = strtok(NULL, " \n");
        char name[256];
        strcpy(name, tok);
        tok = strtok(NULL, " \n");
        do_upload(name, tok);
    }else if(strcmp(tok, "exit")==0){
        running = 0;
    } else {
        printf("Invalid command %s\n", tok);
    }
}

char* prompt(EditLine* e) {
    return "> ";
}

int main(int argc, char** argv){
    parse_opts(argc, argv);

    if(argc < 2){
        printf("USAGE: client hostname\n");
        return 0;
    }

    printf("%s\n", argv[optind]);

    setup(argv[optind]);

    help_message();

    /* disable tab completion */
    /* rl_bind_key('\t', rl_abort); */

    EditLine* el;
    History* hist;
    HistEvent ev;

    int linelen;

    el = el_init(argv[0], stdin, stdout, stderr);
    el_set(el, EL_PROMPT, &prompt);
    el_set(el, EL_EDITOR, "emacs");

    hist = history_init();
    history(hist, &ev, H_SETSIZE, 800);

    el_set(el, EL_HIST, history, hist);

    while(running && (line = (char*)el_gets(el, &linelen)) != NULL){
        do_command(line);

        if(linelen!=0){
            history(hist, &ev, H_ENTER, line);
        }
    }

    free(line);
    history_end(hist);
    el_end(el);

    smdp_write_int(sockfd, SMDP_CLOSE);

    close(sockfd);

    return 0;
}
