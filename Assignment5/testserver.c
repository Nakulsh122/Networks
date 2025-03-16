/*
=====================================
Assignment 5 Submission
Name: [Your_Name]
Roll number: [Your_Roll_Number]
=====================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_TASK_SIZE 100
#define MAX_TASKS 100
#define MAX_CLIENTS 10
#define PORT 8080
#define BUFFER_SIZE 1024

typedef struct
{
    char task[MAX_TASK_SIZE];
    int assigned;
    int completed;
    int client_id;
} Task;

typedef struct
{
    int socket;
    int active;
    int task_id;
    int processing;
} Client;

Task task_queue[MAX_TASKS];
int task_count = 0;
Client clients[MAX_CLIENTS];
int pid_map[MAX_CLIENTS];

void read_tasks_from_file(char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("Error opening task file");
        exit(EXIT_FAILURE);
    }

    while (fgets(task_queue[task_count].task, MAX_TASK_SIZE, file) && task_count < MAX_TASKS)
    {
        // Remove newline character if present
        int len = strlen(task_queue[task_count].task);
        if (len > 0 && task_queue[task_count].task[len - 1] == '\n')
        {
            task_queue[task_count].task[len - 1] = '\0';
        }
        task_queue[task_count].assigned = 0;
        task_queue[task_count].completed = 0;
        task_queue[task_count].client_id = -1;
        task_count++;
    }

    fclose(file);
    printf("Loaded %d tasks from file\n", task_count);
}

void init_clients()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket = -1;
        clients[i].active = 0;
        clients[i].task_id = -1;
        clients[i].processing = 0;
        pid_map[i] = -1;
    }
}

void handle_sigchld(int sig)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        printf("Child process %d terminated\n", pid);

        // Find the client index for this pid
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (pid_map[i] == pid)
            {
                printf("Cleaning up client %d resources\n", i);
                pid_map[i] = -1;

                // If client was processing a task, mark it as unassigned
                if (clients[i].processing && clients[i].task_id >= 0)
                {
                    task_queue[clients[i].task_id].assigned = 0;
                    task_queue[clients[i].task_id].client_id = -1;
                    printf("Task %d returned to queue\n", clients[i].task_id);
                }

                // Close socket if still open
                if (clients[i].active && clients[i].socket >= 0)
                {
                    close(clients[i].socket);
                }

                clients[i].active = 0;
                clients[i].socket = -1;
                clients[i].task_id = -1;
                clients[i].processing = 0;
                break;
            }
        }
    }
}

int find_available_task()
{
    for (int i = 0; i < task_count; i++)
    {
        if (!task_queue[i].assigned && !task_queue[i].completed)
        {
            return i;
        }
    }
    return -1;
}

int find_available_client_slot()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!clients[i].active)
        {
            return i;
        }
    }
    return -1;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <task_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Read tasks from file
    read_tasks_from_file(argv[1]);

    // Initialize client array
    init_clients();

    // Set up signal handler for SIGCHLD to handle child termination
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Define server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, 10) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Task Queue Server started on port %d\n", PORT);

    // Set server socket to non-blocking
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    while (1)
    {
        // Accept new connections (non-blocking)
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (new_socket >= 0)
        {
            printf("New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            // Find an available client slot
            int client_index = find_available_client_slot();
            if (client_index >= 0)
            {
                // Set the client socket to non-blocking
                int flags = fcntl(new_socket, F_GETFL, 0);
                fcntl(new_socket, F_SETFL, flags | O_NONBLOCK);

                // Store client information
                clients[client_index].socket = new_socket;
                clients[client_index].active = 1;
                clients[client_index].task_id = -1;
                clients[client_index].processing = 0;

                printf("Client assigned to slot %d\n", client_index);
            }
            else
            {
                // No available slots, reject connection
                char *msg = "Server is full. Try again later.\n";
                send(new_socket, msg, strlen(msg), 0);
                close(new_socket);
                printf("Connection rejected: server is full\n");
            }
        }
        else if (errno != EWOULDBLOCK && errno != EAGAIN)
        {
            perror("accept failed");
        }

        // Check for client requests
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].active)
            {
                char buffer[BUFFER_SIZE] = {0};
                ssize_t bytes_read = recv(clients[i].socket, buffer, BUFFER_SIZE - 1, MSG_DONTWAIT);

                if (bytes_read > 0)
                {
                    buffer[bytes_read] = '\0';
                    printf("Received from client %d: %s\n", i, buffer);

                    // Process client request
                    if (strncmp(buffer, "GET_TASK", 8) == 0)
                    {
                        // Client is requesting a task
                        if (clients[i].processing)
                        {
                            // Client is already processing a task
                            char *msg = "Error: You are already processing a task\n";
                            send(clients[i].socket, msg, strlen(msg), 0);
                        }
                        else
                        {
                            // Find an available task
                            int task_id = find_available_task();
                            if (task_id >= 0)
                            {
                                // Assign the task to this client
                                task_queue[task_id].assigned = 1;
                                task_queue[task_id].client_id = i;
                                clients[i].task_id = task_id;
                                clients[i].processing = 1;

                                // Send the task to the client
                                char task_msg[BUFFER_SIZE];
                                snprintf(task_msg, BUFFER_SIZE, "Task: %s\n", task_queue[task_id].task);
                                send(clients[i].socket, task_msg, strlen(task_msg), 0);

                                printf("Task %d assigned to client %d: %s\n", task_id, i, task_queue[task_id].task);

                                // Fork a child process to handle this task
                                pid_t pid = fork();
                                if (pid == 0)
                                {
                                    // Child process
                                    sleep(1); // Simulate some processing time
                                    exit(0);
                                }
                                else if (pid > 0)
                                {
                                    // Parent process
                                    pid_map[i] = pid;
                                    printf("Child process %d created for client %d\n", pid, i);
                                }
                                else
                                {
                                    perror("fork failed");
                                }
                            }
                            else
                            {
                                // No available tasks
                                char *msg = "No tasks available\n";
                                send(clients[i].socket, msg, strlen(msg), 0);
                                printf("No tasks available for client %d\n", i);
                            }
                        }
                    }
                    else if (strncmp(buffer, "RESULT", 6) == 0)
                    {
                        // Client is sending a result
                        if (clients[i].processing && clients[i].task_id >= 0)
                        {
                            // Extract the result
                            int result;
                            sscanf(buffer, "RESULT %d", &result);

                            // Mark the task as completed
                            task_queue[clients[i].task_id].completed = 1;

                            // Reset client state
                            clients[i].processing = 0;
                            clients[i].task_id = -1;

                            printf("Task completed by client %d with result: %d\n", i, result);

                            // Confirm to the client
                            char *msg = "Result received\n";
                            send(clients[i].socket, msg, strlen(msg), 0);
                        }
                        else
                        {
                            // Client is not processing any task
                            char *msg = "Error: You are not processing any task\n";
                            send(clients[i].socket, msg, strlen(msg), 0);
                        }
                    }
                    else if (strncmp(buffer, "exit", 4) == 0)
                    {
                        // Client is disconnecting
                        printf("Client %d is disconnecting\n", i);

                        // If client was processing a task, mark it as unassigned
                        if (clients[i].processing && clients[i].task_id >= 0)
                        {
                            task_queue[clients[i].task_id].assigned = 0;
                            task_queue[clients[i].task_id].client_id = -1;
                            printf("Task %d returned to queue\n", clients[i].task_id);
                        }

                        // Close socket and reset client state
                        close(clients[i].socket);
                        clients[i].active = 0;
                        clients[i].socket = -1;
                        clients[i].task_id = -1;
                        clients[i].processing = 0;
                    }
                    else
                    {
                        // Unknown command
                        char *msg = "Unknown command\n";
                        send(clients[i].socket, msg, strlen(msg), 0);
                    }
                }
                else if (bytes_read == 0)
                {
                    // Client disconnected
                    printf("Client %d disconnected\n", i);

                    // If client was processing a task, mark it as unassigned
                    if (clients[i].processing && clients[i].task_id >= 0)
                    {
                        task_queue[clients[i].task_id].assigned = 0;
                        task_queue[clients[i].task_id].client_id = -1;
                        printf("Task %d returned to queue\n", clients[i].task_id);
                    }

                    // Close socket and reset client state
                    close(clients[i].socket);
                    clients[i].active = 0;
                    clients[i].socket = -1;
                    clients[i].task_id = -1;
                    clients[i].processing = 0;
                }
                else if (errno != EWOULDBLOCK && errno != EAGAIN)
                {
                    // Error occurred
                    perror("recv failed");

                    // Close socket and reset client state
                    close(clients[i].socket);
                    clients[i].active = 0;
                    clients[i].socket = -1;
                    clients[i].task_id = -1;
                    clients[i].processing = 0;
                }
            }
        }

        // Check if all tasks are completed
        int all_tasks_completed = 1;
        for (int i = 0; i < task_count; i++)
        {
            if (!task_queue[i].completed)
            {
                all_tasks_completed = 0;
                break;
            }
        }

        if (all_tasks_completed && task_count > 0)
        {
            printf("All tasks have been completed\n");
            // You can choose to exit or continue running the server
            // Uncomment the following line to exit when all tasks are completed
            // break;
        }

        // Add a small delay to prevent CPU hogging
        usleep(10000); // 10ms
    }

    // Clean up
    close(server_fd);
    return 0;
}