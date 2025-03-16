/*
=====================================
Assignment 4 Submission
Name: Nakul Sharma
Roll number: 22CS10046
=====================================
*/

#include "ksocket.h"

/* Global variables */
int shm_id = -1;         // Shared memory ID
ktp_socket *sm = NULL;   // Pointer to shared memory
int init_done = 0;       // Flag to check if initialization is done
int global_error = 0;    // Global error variable

/* Function to simulate message loss */
int dropMessage(float p) {
    float random_val = (float)rand() / RAND_MAX;
    return (random_val < p) ? 1 : 0;
}

/* Helper function to find a KTP socket entry from socket descriptor */
ktp_socket* find_ktp_socket(int sockfd) {
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || !sm[sockfd].is_allocated) {
        return NULL;
    }
    return &sm[sockfd];
}

/* Helper function to check if the shared memory is initialized */
void check_initialization() {
    if (!init_done) {
        // Try to access the shared memory
        key_t key = ftok("/tmp", 'K');
        shm_id = shmget(key, sizeof(ktp_socket) * MAX_KTP_SOCKETS, 0666);
        
        if (shm_id == -1) {
            fprintf(stderr, "Error: KTP initialization not done. Run initksocket first.\n");
            exit(EXIT_FAILURE);
        }
        
        sm = (ktp_socket *)shmat(shm_id, NULL, 0);
        if (sm == (ktp_socket *)-1) {
            perror("shmat");
            exit(EXIT_FAILURE);
        }
        
        init_done = 1;
    }
}

/*
 * k_socket - Create a KTP socket
 */
int k_socket(int domain, int type, int protocol) {
    check_initialization();
    
    if (type != SOCK_KTP) {
        errno = EINVAL;
        return -1;
    }
    
    // Find a free slot in the socket table
    int i;
    for (i = 0; i < MAX_KTP_SOCKETS; i++) {
        if (!sm[i].is_allocated) {
            break;
        }
    }
    
    if (i == MAX_KTP_SOCKETS) {
        global_error = ENOSPACE;
        return -1;
    }
    
    // Create a UDP socket
    int udp_socket = socket(domain, SOCK_DGRAM, protocol);
    if (udp_socket < 0) {
        return -1; // Error creating UDP socket, errno already set
    }
    
    // Initialize the KTP socket structure
    sm[i].is_allocated = 1;
    sm[i].process_id = getpid();
    sm[i].udp_socket = udp_socket;
    sm[i].is_bound = 0;
    
    // Initialize sender side
    sm[i].swnd.base = 1;
    sm[i].swnd.next_seq_num = 1;
    sm[i].swnd.window_size = MAX_WINDOW_SIZE;
    sm[i].send_buffer_start = 0;
    sm[i].send_buffer_end = 0;
    pthread_mutex_init(&sm[i].send_mutex, NULL);
    
    // Initialize receiver side
    sm[i].rwnd.base = 1;
    sm[i].rwnd.window_size = MAX_WINDOW_SIZE;
    sm[i].recv_buffer_start = 0;
    sm[i].recv_buffer_end = 0;
    sm[i].recv_buffer_count = 0;
    sm[i].nospace_flag = 0;
    sm[i].last_ack_sent = 0;
    pthread_mutex_init(&sm[i].recv_mutex, NULL);
    
    return i; // Return the KTP socket descriptor
}

/*
 * k_bind - Bind a KTP socket to local and remote addresses
 */
int k_bind(int sockfd, const struct sockaddr *src_addr, socklen_t addrlen, 
          const struct sockaddr *dest_addr, socklen_t dest_addrlen) {
    check_initialization();
    
    ktp_socket *ks = find_ktp_socket(sockfd);
    if (!ks) {
        errno = EBADF;
        return -1;
    }
    
    // Bind the UDP socket to the source address
    int result = bind(ks->udp_socket, src_addr, addrlen);
    if (result < 0) {
        return -1; // Error binding UDP socket, errno already set
    }
    
    // Store the source and destination addresses
    memcpy(&ks->src_addr, src_addr, addrlen);
    memcpy(&ks->dest_addr, dest_addr, dest_addrlen);
    
    ks->is_bound = 1;
    return 0;
}

/*
 * k_sendto - Send a message over a KTP socket
 */
ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen) {
    check_initialization();
    
    ktp_socket *ks = find_ktp_socket(sockfd);
    if (!ks) {
        errno = EBADF;
        return -1;
    }
    
    if (!ks->is_bound) {
        global_error = ENOTBOUND;
        return -1;
    }
    
    // Check if destination matches the bound one
    struct sockaddr_in *dest = (struct sockaddr_in *)dest_addr;
    if (dest->sin_addr.s_addr != ks->dest_addr.sin_addr.s_addr || 
        dest->sin_port != ks->dest_addr.sin_port) {
        global_error = ENOTBOUND;
        return -1;
    }
    
    pthread_mutex_lock(&ks->send_mutex);
    
    // Check if there's space in the send buffer
    int next_pos = (ks->send_buffer_end + 1) % MAX_BUFFER_SIZE;
    if (next_pos == ks->send_buffer_start) {
        pthread_mutex_unlock(&ks->send_mutex);
        global_error = ENOSPACE;
        return -1;
    }
    
    // Prepare the message
    ktp_message *msg = &ks->send_buffer[ks->send_buffer_end];
    msg->type = DATA_MSG;
    msg->seq_num = 0; // Will be set by the sender thread
    msg->rwnd_size = 0;
    
    // Copy the data
    memset(msg->data, 0, MSG_SIZE);
    size_t copy_len = (len < MSG_SIZE) ? len : MSG_SIZE;
    memcpy(msg->data, buf, copy_len);
    
    // Update the buffer end pointer
    ks->send_buffer_end = next_pos;
    
    pthread_mutex_unlock(&ks->send_mutex);
    
    return copy_len;
}

/*
 * k_recvfrom - Receive a message from a KTP socket
 */
ssize_t k_recvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen) {
    check_initialization();
    
    ktp_socket *ks = find_ktp_socket(sockfd);
    if (!ks) {
        errno = EBADF;
        return -1;
    }
    
    pthread_mutex_lock(&ks->recv_mutex);
    
    // Check if there's a message in the receive buffer
    if (ks->recv_buffer_start == ks->recv_buffer_end) {
        pthread_mutex_unlock(&ks->recv_mutex);
        global_error = ENOMESSAGE;
        return -1;
    }
    
    // Get the message from the buffer
    ktp_message *msg = &ks->recv_buffer[ks->recv_buffer_start];
    
    // Copy the data to the user buffer
    memset(buf, 0, len);
    size_t copy_len = (len < MSG_SIZE) ? len : MSG_SIZE;
    memcpy(buf, msg->data, copy_len);
    
    // Update the buffer start pointer
    ks->recv_buffer_start = (ks->recv_buffer_start + 1) % MAX_BUFFER_SIZE;
    ks->recv_buffer_count--;
    
    // Update the nospace flag
    if (ks->nospace_flag && ks->recv_buffer_count < MAX_BUFFER_SIZE) {
        ks->nospace_flag = 0;
    }
    
    pthread_mutex_unlock(&ks->recv_mutex);
    
    // Set the source address if requested
    if (src_addr && addrlen) {
        memcpy(src_addr, &ks->src_addr, *addrlen);
    }
    
    return copy_len;
}

/*
 * k_close - Close a KTP socket
 */
int k_close(int sockfd) {
    check_initialization();
    
    ktp_socket *ks = find_ktp_socket(sockfd);
    if (!ks) {
        errno = EBADF;
        return -1;
    }
    
    // Close the UDP socket
    close(ks->udp_socket);
    
    // Destroy mutexes
    pthread_mutex_destroy(&ks->send_mutex);
    pthread_mutex_destroy(&ks->recv_mutex);
    
    // Mark the socket as free
    ks->is_allocated = 0;
    
    return 0;
}
