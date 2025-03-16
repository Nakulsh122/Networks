/* Assignment 3 Submission
   Name: Nakul Sharma
   Roll number: 22CS10046
   Link of the pcap file: In the zip file 
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define PORT 8080
#define BUFFER_SIZE 100

void encrypt(char *buffer, const char *key) {
    for (int i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] >= 'A' && buffer[i] <= 'Z') {
            buffer[i] = key[buffer[i] - 'A'];
        } else if (buffer[i] >= 'a' && buffer[i] <= 'z') {
            buffer[i] = key[buffer[i] - 'a'] + 32;
        }
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];
    char key[27];

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        perror("Listen");
        exit(EXIT_FAILURE);
    }
    printf("Waiting for connection...\n");

    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0) {
        printf("Connection established!\n");
        read(new_socket, key, 26);
        key[26] = '\0';

        int file_fd = open("received.txt", O_WRONLY | O_CREAT, 0644);
        if (file_fd < 0) {
            perror("File open error");
            exit(EXIT_FAILURE);
        }
        ssize_t bytes_read;
        while ((bytes_read = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
            buffer[bytes_read] = '\0';
            encrypt(buffer, key);
            write(file_fd, buffer, bytes_read);
        }
        close(file_fd);
        printf("File received and encrypted\n");
        close(new_socket);
    }

    return 0;
}