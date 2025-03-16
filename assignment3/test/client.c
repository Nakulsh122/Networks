#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>


// argv[0] = filename
// argv[1] = server_ipaddr
// arg[2] = portno
void error(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc,char *argv[]){
    if(argc < 3 ){
        fprintf(stderr,"Please povide all required arguments");
        exit(1);
    }

    int sockfd , portno , m;
    struct sockaddr_in serv_addr;
    struct hostnet *server;

    char buffer[256];
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET,SOCK_STREAM , 0);
    if(sockfd < 0){
        error("Socket creation failed");
    }
   serv_addr.sin_family = AF_INET ; 
   serv_addr.sin_port = htons(portno);
   if(connect(sockfd , (struct sockaddr *)&serv_addr,sizeof(serv_addr))<0){
    error("connection failed");
   }

    FILE *f ;
    int words = 0 ; 
    char c ;
    f = fopen("sample.txt", "r");
    while((c = getc(f)) != EOF){
        fscanf(f, "%s" , buffer);
        if(isspace(c) || c =='\t'){
            words++;
        }
    }

    write(sockfd,&words , sizeof(int));
    rewind(f);

    char ch;
    while(ch != EOF){
        fscanf(f ,"%s" ,  buffer);
        write(sockfd,buffer,255);
        ch = fgetc(f);
    }

    printf("File transfer complete ....");
    fclose(f);

   close(sockfd);
    return 0 ;
}