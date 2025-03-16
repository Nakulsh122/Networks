/*
=====================================
Assignment 4 Submission
Name: Nakul Sharma
Roll number: 22CS10046
=====================================
*/

#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>

/* Configuration parameters */
#define T 5            // Timeout value in seconds
#define P 0.2          // Probability of packet loss
#define MAX_KTP_SOCKETS 10    // Maximum number of KTP sockets
#define SOCK_KTP 5            // Socket type for KTP
#define MSG_SIZE 512          // Fixed message size in bytes
#define MAX_SEQ_NUM 256       // Maximum sequence number (8-bit)
#define MAX_WINDOW_SIZE 10    // Maximum window size
#define MAX_BUFFER_SIZE 20    // Buffer size at sender and receiver

/* Custom error codes */
#define ENOSPACE 1001         // No space available in buffer or socket table
#define ENOTBOUND 1002        // Socket not bound to any address
#define ENOMESSAGE 1003       // No message available in receive buffer

/* Message types */
#define DATA_MSG 1
#define ACK_MSG 2

/* KTP Message format */
typedef struct {
    uint8_t type;             // Message type (DATA_MSG or ACK_MSG)
    uint8_t seq_num;          // Sequence number
    uint8_t ack_num;          // Acknowledgment number (for ACK messages)
    uint8_t rwnd_size;        // Receiver window size (piggybacked in ACK)
    char data[MSG_SIZE];      // Data payload
} ktp_message;

/* Window structure */
typedef struct {
    uint8_t base;             // Base sequence number of the window
    uint8_t next_seq_num;     // Next sequence number to use
    uint8_t window_size;      // Current window size
    struct timeval send_time[MAX_WINDOW_SIZE]; // Time when each message was sent
    ktp_message messages[MAX_WINDOW_SIZE];    // Buffer for messages in the window
} window;

/* KTP socket structure stored in shared memory */
typedef struct {
    int is_allocated;         // 1 if socket is allocated, 0 otherwise
    pid_t process_id;         // Process ID that created this socket
    int udp_socket;           // Associated UDP socket
    struct sockaddr_in src_addr;  // Source address
    struct sockaddr_in dest_addr; // Destination address
    int is_bound;             // 1 if socket is bound, 0 otherwise
    
    /* Sender side */
    window swnd;              // Sending window
    ktp_message send_buffer[MAX_BUFFER_SIZE]; // Sender buffer
    int send_buffer_start;    // Start index of send buffer
    int send_buffer_end;      // End index of send buffer
    pthread_mutex_t send_mutex; // Mutex for send buffer
    
    /* Receiver side */
    window rwnd;              // Receiving window
    ktp_message recv_buffer[MAX_BUFFER_SIZE]; // Receiver buffer
    int recv_buffer_start;    // Start index of receive buffer
    int recv_buffer_end;      // End index of receive buffer
    int recv_buffer_count;    // Number of messages in receive buffer
    pthread_mutex_t recv_mutex; // Mutex for receive buffer
    int nospace_flag;         // Flag to indicate if receive buffer is full
    uint8_t last_ack_sent;    // Last acknowledgment sent
} ktp_socket;

/* Function prototypes */
int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, const struct sockaddr *src_addr, socklen_t addrlen, 
          const struct sockaddr *dest_addr, socklen_t dest_addrlen);
ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t k_recvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen);
int k_close(int sockfd);
int dropMessage(float p);

/* Global variables */
extern int shm_id;          // Shared memory ID
extern ktp_socket *sm;      // Pointer to shared memory
extern int init_done;       // Flag to check if initialization is done
extern int global_error;

#endif /* KSOCKET_H */
