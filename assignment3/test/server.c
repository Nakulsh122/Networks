#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc,char *argv[]){
    if(argc < 2 ){
        fprintf(stderr,"Please povide port number , program terminated.");
        exit(1);
    }
    int sockfd,newsockfd;
    int portno;
    char buffer[255];
    struct sockaddr_in server_addr, client_addr ;
    socklen_t clientlen; //32 bit datatype gives us the internet address 

    sockfd = socket(AF_INET,SOCK_STREAM,0);
    if(sockfd < 0 ){
        printf("socket initiation failed ");
        exit(1);
    }
    bzero((char *) &server_addr ,sizeof(server_addr));
    portno = atoi(argv[1]);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(portno);

    if(bind(sockfd,(struct sockaddr *) &server_addr,sizeof(server_addr))<0){
        error("binding failed");
    }

    listen(sockfd,5);
    clientlen = sizeof(client_addr);
    newsockfd = accept(sockfd , (struct sockaddr *)&client_addr , &clientlen);
    if(newsockfd < 0 ){
        error("error on accept");
    }
    FILE *f ;
    int ch ; 

    f = fopen("recieved.txt","a");
    int words ; 
    read(newsockfd, &words ,sizeof(int));
  
    while (ch < (words-1)){
        read(newsockfd,buffer , 255);
        fprintf(f,"%s",buffer );
        ch++;
    }

    // while(1){
    //     bzero(buffer , 255);
    //     int n = read(newsockfd , buffer , 255);
    //     if(n < 0){
    //         error("Error while reading ");
    //     }

    //     printf("client : %s\n",buffer);

    //     bzero(buffer,255);
    //     fgets(buffer , 255 , stdin);
    //     n = write(newsockfd , buffer , strlen(buffer));
    //     if(n<0){
    //         error("error while writing ");
    //     }

    //     int l = strncmp("byeee",buffer,5);
    //     if(l == 0 ){
    //         break;
    //     }
    // }
    fclose(f);
    close(newsockfd);
    close(sockfd);
    return 0 ;
}
