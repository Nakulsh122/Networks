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
    
    // Open file to send
    char filename[256];
    printf("Enter the name of the file to send: ");
    scanf("%255s", filename);
    
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        k_close(sockfd);
        return 1;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    printf("Sending file: %s (%ld bytes)\n", filename, file_size);
    
    // Send the file
    char buffer[BUFFER_SIZE];
    int total_sent = 0;
    int transmissions = 0;
    
    while (!feof(fp)) {
        // Read a chunk from the file
        int bytes_read = fread(buffer, 1, BUFFER_SIZE, fp);
        if (bytes_read <= 0) break;
        
        // Attempt to send until successful
        int sent = k_sendto(sockfd, buffer, bytes_read, 0, 
                         (struct sockaddr*)&remote_addr, sizeof(remote_addr));
        
        if (sent < 0) {
            // If buffer is full, wait a bit and retry
            if (global_error == ENOSPACE) {
                usleep(100000);  // Sleep for 100ms
                fseek(fp, -bytes_read, SEEK_CUR);  // Move file pointer back
                continue;
            } else {
                printf("Error sending data: %d\n", global_error);
                break;
            }
        }
        
        total_sent += bytes_read;
        transmissions++;
        
        // Display progress
        float progress = (float)total_sent / file_size * 100;
        printf("\rProgress: %.2f%% (%d/%ld bytes)", progress, total_sent, file_size);
        fflush(stdout);
    }
    
    printf("\nFile transfer complete. Sent %d bytes in %d messages.\n", 
           total_sent, transmissions);
    
    // Close the file and the socket
    fclose(fp);
    k_close(sockfd);
    
    return 0;
}
