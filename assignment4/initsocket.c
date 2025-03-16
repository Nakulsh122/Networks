/*
=====================================
Assignment 4 Submission
Name: Nakul Sharma
Roll number: 22CS10046
=====================================
*/

#include "ksocket.h"
#include <fcntl.h>  // Added for fcntl

/* Global variables */
int running = 1;
fd_set master_set;
int max_fd = 0;
// Remove these definitions since they will be in ksocket.c
// int shm_id = -1;
// ktp_socket *sm = NULL;

/* Function prototypes for threads */
void* receiver_thread(void* arg);
void* sender_thread(void* arg);
void cleanup_handler(int sig);
void garbage_collector();

/* Function to create shared memory */
void create_shared_memory() {
    key_t key = ftok("/tmp", 'K');
    shm_id = shmget(key, sizeof(ktp_socket) * MAX_KTP_SOCKETS, IPC_CREAT | 0666);
    
    if (shm_id == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    
    sm = (ktp_socket *)shmat(shm_id, NULL, 0);
    if (sm == (ktp_socket *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    
    // Initialize the socket table
    memset(sm, 0, sizeof(ktp_socket) * MAX_KTP_SOCKETS);
    
    // Initialize all socket descriptors to -1
    for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
        sm[i].udp_socket = -1;
    }
}

/* Function to send a data message */
void send_data_message(ktp_socket *ks, ktp_message *msg) {
    if (ks->udp_socket < 0) {
        printf("Warning: Trying to send on invalid socket\n");
        return;
    }
    
    msg->type = DATA_MSG;
    
    // Send the message through UDP
    sendto(ks->udp_socket, msg, sizeof(ktp_message), 0,
           (struct sockaddr*)&ks->dest_addr, sizeof(struct sockaddr_in));
    
    // Record send time
    gettimeofday(&ks->swnd.send_time[msg->seq_num % MAX_WINDOW_SIZE], NULL);
}

/* Function to send an ACK message */
void send_ack_message(ktp_socket *ks, uint8_t ack_num) {
    if (ks->udp_socket < 0) {
        printf("Warning: Trying to send ACK on invalid socket\n");
        return;
    }
    
    ktp_message ack_msg;
    
    // Prepare ACK message
    ack_msg.type = ACK_MSG;
    ack_msg.seq_num = 0;  // Not used in ACK
    ack_msg.ack_num = ack_num;
    
    pthread_mutex_lock(&ks->recv_mutex);
    // Calculate available space in receive buffer
    ack_msg.rwnd_size = MAX_BUFFER_SIZE - ks->recv_buffer_count;
    ks->last_ack_sent = ack_num;
    pthread_mutex_unlock(&ks->recv_mutex);
    
    // Send the ACK through UDP
    sendto(ks->udp_socket, &ack_msg, sizeof(ktp_message), 0,
           (struct sockaddr*)&ks->dest_addr, sizeof(struct sockaddr_in));
}

/* Process received data message */
void process_data_message(ktp_socket *ks, ktp_message *msg) {
    pthread_mutex_lock(&ks->recv_mutex);
    
    uint8_t seq_num = msg->seq_num;
    uint8_t expected_seq = ks->rwnd.base;
    
    // Check if message is within the receive window
    if ((seq_num >= expected_seq) && 
        (seq_num < expected_seq + ks->rwnd.window_size)) {
        
        // Check if we have space in the buffer
        if (ks->recv_buffer_count < MAX_BUFFER_SIZE) {
            // Store the message in the buffer if it's the next expected one
            if (seq_num == expected_seq) {
                // Store in receive buffer
                memcpy(&ks->recv_buffer[ks->recv_buffer_end], msg, sizeof(ktp_message));
                ks->recv_buffer_end = (ks->recv_buffer_end + 1) % MAX_BUFFER_SIZE;
                ks->recv_buffer_count++;
                
                // Slide the window
                ks->rwnd.base++;
                
                // Check if receive buffer is now full
                if (ks->recv_buffer_count >= MAX_BUFFER_SIZE) {
                    ks->nospace_flag = 1;
                }
                
                // Send ACK for this message
                pthread_mutex_unlock(&ks->recv_mutex);
                send_ack_message(ks, seq_num);
                return;
            }
            // Out of order message within window
            // In a more complex implementation, we would buffer this
            // For simplicity, we'll just drop it
        } else {
            // Buffer is full
            ks->nospace_flag = 1;
        }
    }
    
    // If we reached here, either:
    // 1. Message is outside window
    // 2. Message is out of order
    // 3. Buffer is full
    // We will not send an ACK in these cases
    
    pthread_mutex_unlock(&ks->recv_mutex);
}

/* Process received ACK message */
void process_ack_message(ktp_socket *ks, ktp_message *msg) {
    pthread_mutex_lock(&ks->send_mutex);
    
    uint8_t ack_num = msg->ack_num;
    uint8_t current_base = ks->swnd.base;
    
    // Check if ACK is valid (within current window)
    if ((ack_num >= current_base) && 
        (ack_num < current_base + ks->swnd.window_size)) {
        
        // Slide the window up to the acknowledged message
        ks->swnd.base = ack_num + 1;
        
        // Update window size based on receiver's advertised window
        ks->swnd.window_size = msg->rwnd_size;
        if (ks->swnd.window_size > MAX_WINDOW_SIZE) {
            ks->swnd.window_size = MAX_WINDOW_SIZE;
        }
    }
    
    pthread_mutex_unlock(&ks->send_mutex);
}

/* Function to verify if a socket is valid */
int is_socket_valid(int sock) {
    if (sock < 0) return 0;
    
    // Try to get socket flags to check if it's a valid descriptor
    if (fcntl(sock, F_GETFD) == -1) {
        if (errno == EBADF) {
            return 0;  // Invalid descriptor
        }
    }
    return 1;  // Valid descriptor
}

/* Receiver thread function */
void* receiver_thread(void* arg) {
    fd_set read_fds;
    struct timeval timeout;
    
    // Initialize master_set
    FD_ZERO(&master_set);
    
    while (running) {
        // Reset the read_fds set each time
        FD_ZERO(&read_fds);
        max_fd = 0;  // Reset max_fd
        
        // Add valid sockets to the set for this iteration
        for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
            if (sm[i].is_allocated && sm[i].udp_socket >= 0) {
                // Verify socket is still valid before adding to set
                if (is_socket_valid(sm[i].udp_socket)) {
                    FD_SET(sm[i].udp_socket, &read_fds);
                    if (sm[i].udp_socket > max_fd) {
                        max_fd = sm[i].udp_socket;
                    }
                } else {
                    // Socket is invalid, mark it as such
                    printf("Found invalid socket at index %d, marking as closed\n", i);
                    sm[i].udp_socket = -1;
                }
            }
        }
        
        // Skip select if no valid sockets
        if (max_fd == 0) {
            sleep(1);
            continue;
        }
        
        timeout.tv_sec = 1;  // Check every second
        timeout.tv_usec = 0;
        
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal
            } else if (errno == EBADF) {
                // Bad file descriptor, reset and rebuild the fd_set
                printf("select: Bad file descriptor, resetting file descriptor sets\n");
                FD_ZERO(&master_set);
                max_fd = 0;
                continue;
            }
            perror("select");
            continue;
        }
        
        if (ready == 0) {
            // Timeout occurred, no data available
            continue;
        }
        
        // Check for data from existing sockets
        for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
            if (!sm[i].is_allocated || sm[i].udp_socket < 0) continue;
            
            if (FD_ISSET(sm[i].udp_socket, &read_fds)) {
                ktp_message msg;
                struct sockaddr_in sender_addr;
                socklen_t sender_len = sizeof(sender_addr);
                
                // Receive message
                int bytes = recvfrom(sm[i].udp_socket, &msg, sizeof(ktp_message), 0,
                                    (struct sockaddr*)&sender_addr, &sender_len);
                
                if (bytes < 0) {
                    if (errno == EBADF) {
                        printf("Socket %d is bad, marking as closed\n", sm[i].udp_socket);
                        sm[i].udp_socket = -1;
                    } else {
                        perror("recvfrom");
                    }
                    continue;
                }
                
                // Simulate message loss
                if (dropMessage(P)) {
                    printf("Dropping message (type: %d, seq: %d)\n", msg.type, msg.seq_num);
                    continue;
                }
                
                // Process the message based on its type
                if (msg.type == DATA_MSG) {
                    process_data_message(&sm[i], &msg);
                } else if (msg.type == ACK_MSG) {
                    process_ack_message(&sm[i], &msg);
                }
            }
        }
        
        // Check for buffer space becoming available
        for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
            if (!sm[i].is_allocated) continue;
            
            pthread_mutex_lock(&sm[i].recv_mutex);
            if (sm[i].nospace_flag == 1 && sm[i].recv_buffer_count < MAX_BUFFER_SIZE) {
                sm[i].nospace_flag = 0;
                // Send duplicate ACK with updated window size
                uint8_t last_ack = sm[i].last_ack_sent;
                pthread_mutex_unlock(&sm[i].recv_mutex);
                send_ack_message(&sm[i], last_ack);
            } else {
                pthread_mutex_unlock(&sm[i].recv_mutex);
            }
        }
    }
    
    return NULL;
}

/* Sender thread function */
void* sender_thread(void* arg) {
    struct timeval current_time;
    
    while (running) {
        // Sleep for T/2 seconds
        struct timespec sleep_time;
        sleep_time.tv_sec = T / 2;
        sleep_time.tv_nsec = (T % 2) * 500000000;
        nanosleep(&sleep_time, NULL);
        
        // Get current time
        gettimeofday(&current_time, NULL);
        
        // Check all KTP sockets
        for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
            if (!sm[i].is_allocated || !is_socket_valid(sm[i].udp_socket)) continue;
            
            pthread_mutex_lock(&sm[i].send_mutex);
            
            // Check for timeout
            uint8_t base = sm[i].swnd.base;
            uint8_t next_seq = sm[i].swnd.next_seq_num;
            uint8_t window_size = sm[i].swnd.window_size;
            
            // Retransmit unacknowledged messages on timeout
            for (uint8_t seq = base; seq != next_seq; seq++) {
                int idx = seq % MAX_WINDOW_SIZE;
                struct timeval *send_time = &sm[i].swnd.send_time[idx];
                
                // Check if timeout occurred
                long elapsed = (current_time.tv_sec - send_time->tv_sec) * 1000000 +
                               (current_time.tv_usec - send_time->tv_usec);
                
                if (elapsed > T * 1000000) {  // Convert T to microseconds
                    // Retransmit the message
                    ktp_message *msg = &sm[i].swnd.messages[idx];
                    send_data_message(&sm[i], msg);
                    printf("Retransmitting message (seq: %d)\n", msg->seq_num);
                }
            }
            
            // Send new messages from buffer if there's space in the window
            while ((next_seq < base + window_size) && 
                   (sm[i].send_buffer_start != sm[i].send_buffer_end)) {
                
                // Get message from buffer
                ktp_message msg;
                memcpy(&msg, &sm[i].send_buffer[sm[i].send_buffer_start], sizeof(ktp_message));
                sm[i].send_buffer_start = (sm[i].send_buffer_start + 1) % MAX_BUFFER_SIZE;
                
                // Assign sequence number
                msg.seq_num = next_seq;
                
                // Store in window
                int idx = next_seq % MAX_WINDOW_SIZE;
                memcpy(&sm[i].swnd.messages[idx], &msg, sizeof(ktp_message));
                
                // Send the message
                send_data_message(&sm[i], &msg);
                printf("Sending new message (seq: %d)\n", msg.seq_num);
                
                // Update next sequence number
                next_seq++;
            }
            
            // Update window state
            sm[i].swnd.next_seq_num = next_seq;
            
            pthread_mutex_unlock(&sm[i].send_mutex);
        }
    }
    
    return NULL;
}

/* Garbage collector function */
void garbage_collector() {
    // Check for dead processes and clean up their sockets
    while (running) {
        sleep(5);  // Check every 5 seconds
        
        for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
            if (!sm[i].is_allocated) continue;
            
            // Check if process is still alive
            pid_t pid = sm[i].process_id;
            if (kill(pid, 0) == -1 && errno == ESRCH) {
                // Process does not exist, clean up the socket
                if (sm[i].udp_socket >= 0) {
                    close(sm[i].udp_socket);
                    // Make sure to clear this socket from any fd sets
                    FD_CLR(sm[i].udp_socket, &master_set);
                }
                pthread_mutex_destroy(&sm[i].send_mutex);
                pthread_mutex_destroy(&sm[i].recv_mutex);
                sm[i].is_allocated = 0;
                sm[i].udp_socket = -1;
                printf("Cleaned up socket %d for dead process %d\n", i, pid);
            }
        }
    }
}

/* Signal handler for cleanup */
void cleanup_handler(int sig) {
    printf("Received signal %d, cleaning up...\n", sig);
    running = 0;
    
    // Close all open sockets
    for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
        if (sm[i].is_allocated && sm[i].udp_socket >= 0) {
            close(sm[i].udp_socket);
            sm[i].udp_socket = -1;
        }
    }
    
    // Detach from shared memory
    if (sm != NULL) {
        shmdt(sm);
    }
    
    // Remove shared memory
    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
    }
    
    exit(0);
}

int main() {
    // Seed random number generator
    srand(time(NULL));
    
    // Set up signal handlers
    signal(SIGINT, cleanup_handler);
    signal(SIGTERM, cleanup_handler);
    
    // Create shared memory
    create_shared_memory();
    
    // Create threads
    pthread_t receiver_tid, sender_tid;
    
    if (pthread_create(&receiver_tid, NULL, receiver_thread, NULL) != 0) {
        perror("Failed to create receiver thread");
        cleanup_handler(0);
    }
    
    if (pthread_create(&sender_tid, NULL, sender_thread, NULL) != 0) {
        perror("Failed to create sender thread");
        cleanup_handler(0);
    }
    
    // Start garbage collector as a separate process
    pid_t gc_pid = fork();
    if (gc_pid == 0) {
        // Child process
        garbage_collector();
        exit(0);
    } else if (gc_pid < 0) {
        perror("Failed to create garbage collector process");
        cleanup_handler(0);
    }
    
    printf("KTP initialization complete. Running with:\n");
    printf("Max KTP sockets: %d\n", MAX_KTP_SOCKETS);
    printf("Timeout (T): %d seconds\n", T);
    printf("Drop probability (P): %.2f\n", P);
    
    // Wait for threads
    pthread_join(receiver_tid, NULL);
    pthread_join(sender_tid, NULL);
    
    // Cleanup
    cleanup_handler(0);
    
    return 0;
}