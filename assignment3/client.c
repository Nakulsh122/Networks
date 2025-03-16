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

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char key[27];
    char filename[100];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        return -1;
    }

    printf("Enter the encryption key (26 letters): ");
    scanf("%26s", key);
    send(sock, key, 26, 0);

    printf("Enter filename: ");
    scanf("%99s", filename);

    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        perror("File open error");
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }
    close(file_fd);

    printf("File sent for encryption\n");
    close(sock);
    return 0;
}
