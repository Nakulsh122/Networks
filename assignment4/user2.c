/*
=====================================
Assignment 4 Submission
Name: Nakul Sharma
Roll number: 22CS10046
=====================================
*/

#include "ksocket.h"

#define BUFFER_SIZE 512

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <local_ip> <local_port> <remote_ip> <remote_port>\n", argv[0]);
        return 1;
    }

    char *local_ip = argv[1];
    int local_port = atoi(argv[2]);
    char *remote_ip = argv[3];
    int remote_port = atoi(argv[4]);
    
    // Create a KTP socket
    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("k_socket");
        return 1;
    }
    
    printf("Created KTP socket with descriptor: %d\n", sockfd);
    
    // Set up local and remote addresses
    struct sockaddr_in local_addr, remote_addr;
    
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr(local_ip);
    local_addr.sin_port = htons(local_port);
    
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = inet_addr(remote_ip);
    remote_addr.sin_port = htons(remote_port);
    
    // Bind the socket
    if (k_bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr),
                     (struct sockaddr*)&remote_addr, sizeof(remote_addr)) < 0) {
        perror("k_bind");
        return 1;
    }
    
    printf("Bound KTP socket to %s:%d -> %s:%d\n", 
           local_ip, local_port, remote_ip, remote_port);
    
    // Create output file
    char filename[256];
    printf("Enter a name for the received file: ");
    scanf("%255s", filename);
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        k_close(sockfd);
        return 1;
    }
    
    printf("Receiving file data to: %s\n", filename);
    printf("Press Ctrl+C to stop receiving\n");
    
    // Receive the file
    char buffer[BUFFER_SIZE];
    int total_received = 0;
    int messages_received = 0;
    
    while (1) {
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        
        // Try to receive data
        int bytes = k_recvfrom(sockfd, buffer, BUFFER_SIZE, 0, 
                             (struct sockaddr*)&sender_addr, &sender_len);
        
        if (bytes < 0) {
            if (global_error == ENOMESSAGE) {
                // No message available, wait a bit
                usleep(100000);  // Sleep for 100ms
                continue;
            } else {
                printf("Error receiving data: %d\n", global_error);
                break;
            }
        }
        
        // Write data to file
        fwrite(buffer, 1, bytes, fp);
        
        total_received += bytes;
        messages_received++;
        
        // Display progress
        printf("\rReceived: %d bytes (%d messages)", total_received, messages_received);
        fflush(stdout);
        
        // Flush the file periodically
        if (messages_received % 10 == 0) {
            fflush(fp);
        }
    }
    
    printf("\nFile reception complete. Received %d bytes in %d messages.\n", 
           total_received, messages_received);
    
    // Close the file and the socket
    fclose(fp);
    k_close(sockfd);
    
    return 0;
}
