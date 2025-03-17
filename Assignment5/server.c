/*
=====================================
Assignment 5 Submission
Name: Nakul Sharma
Roll number: 22CS10046
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

// constants used through out the program 

#define PORT 6000
#define MAX_TASKS 100
#define BUFFER 1024
#define MAX_CLIENTS 100
#define TASK_SIZE 100

// structure definations for the clinets and the task 
typedef struct
{
    int assigned; //bool if the task is assigned or not 
    int completed; // if the task is completed or not
    int client; // the client's socket descriptor that is doing the task 
    char task[TASK_SIZE]; // the task as a string to be sent as a message to the client 
} task;

typedef struct
{
    int socket; //the socket descriptor
    int active; //if the client is active or not 
    int processing; //if the client is processing or not 
    int task_id; // the task that it is processing right now 
} client;

// adding the global arrays and variables 
task TasksList[MAX_TASKS]; //list of all the tasks that are present 
client clientList[MAX_CLIENTS]; //list of clients that have joined 
int noTasks = 0; //total no of tasks present 
int client_pids[MAX_CLIENTS]; //pids of the child processes to keep track of the clients and the processes that they are working along
// this is the read file functions it reads the file line by line and then assigns the task to the previously initialised tasksList array and sets all the parameters for the tasks proper funcitoning
void read_tasks(char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        printf("File could not be opened\n");
        exit(1);
    }
    int a, b;
    char op;
    char line[TASK_SIZE];
    while (fgets(line, TASK_SIZE, file) && noTasks < MAX_TASKS)
    {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Formatting the tasks for the proper parsing by the client
        if (sscanf(line, "%d %c %d", &a, &op, &b) == 3) {
            
            snprintf(TasksList[noTasks].task, TASK_SIZE, "%d %c %d", a, op, b);
            TasksList[noTasks].assigned = 0;
            TasksList[noTasks].client = -1;
            TasksList[noTasks].completed = 0;
            noTasks++;
        }
        else {
            printf("Warning: Skipping invalid task format: %s\n", line);
        }
    }
    fclose(file);
    printf("All tasks read and stored (%d tasks total)\n", noTasks);
}
// inititalising the clients as inactive and adding the required attributes 
void init_clients()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clientList[i].active = 0;
        clientList[i].socket = -1;
        clientList[i].processing = 0;
        clientList[i].task_id = -1;
        client_pids[i] = -1;
    }
    printf("Clients Successfully initialised\n");
}

// handling the dead processes that are recieved from the client so that no zombies processes remain
void handle_dead_process(int sig)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        printf("Child process %d deleted\n", pid);

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_pids[i] == pid)
            {
                printf("Cleaning up client task %d\n", i);
                client_pids[i] = -1;
                if (clientList[i].processing && clientList[i].task_id > 0)
                {   int task_id = clientList[i].task_id;
                    TasksList[task_id].assigned = 0;
                    TasksList[task_id].client = -1;
                    printf("Tasks %d returned to the queue\n", clientList[i].task_id); //retruning the tasks that was assigned to the client process
                }

                if (clientList[i].active && clientList[i].socket >= 0)
                {
                    close(clientList[i].socket);
                }
                clientList[i].active = 0;
                clientList[i].socket = -1;
                clientList[i].task_id = -1;
                clientList[i].processing = 0;
                break;
            }
        }
    }
}

// find the tasks that have not been assigned as of now
int find_task()
{
    for (int i = 0; i < noTasks; i++)
    {
        if (!TasksList[i].assigned && !TasksList[i].completed)
        {
            return i;
        }
    }
    return -1;
}

// find the clients that are free to assign a task to 
int find_client()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!clientList[i].active)
        {
            return i;
        }
    }
    return -1;
}
// send message or data function for the server 
void send_to_client(int socket, char *msg)
{
    send(socket, msg, strlen(msg), 0);
    printf("Message send to %d\n", socket);
}
// close any client that is being disconnected and then mark it as inactive in the clientsList
void close_client(int i)
{
    close(clientList[i].socket);
    clientList[i].active = 0;
    clientList[i].socket = -1;
    clientList[i].task_id = -1;
    clientList[i].processing = 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s <tasks_file>\n", argv[0]); //usage prompt to allow the uesr to get the usage format 
        exit(1);
    }

    read_tasks(argv[1]);
    init_clients();
    //inititalising the signal handler 
    struct sigaction sa;
    sa.sa_handler = handle_dead_process;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }
    //socket initialisation for the server 
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        perror("socket creation failed\n");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Socket option setip failed\n");
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bindingfailed\n");
        exit(1);
    }

    if (listen(server_sock, 10) < 0)
    {
        perror("Cannot Listen\n");
        exit(1);
    }

    printf("Task Server Started at Port : %d\n", PORT);
    //adding the flags for the NONBLOCKING behaviour
    int flags = fcntl(server_sock, F_GETFL, 0);
    fcntl(server_sock, F_SETFL, flags | O_NONBLOCK);

    // main server loop
    while (1)
    {
        // get the new client's address 
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_socket = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        // new client joined 
        if (new_socket > 0)
        {
            printf("New connrction from %s : %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            int client_index = find_client();
            // find slots for the client to be put in the clientList
            if (client_index >= 0)
            {
                int flags = fcntl(new_socket, F_SETFL, 0);
                fcntl(new_socket, F_SETFL, flags | O_NONBLOCK);

                clientList[client_index].socket = new_socket;
                clientList[client_index].active = 1;
                clientList[client_index].task_id = -1;
                clientList[client_index].processing = 0;
                printf("client assigned to slot %d\n", client_index);
            }
            else
            {
                char *msg = "No slots available. Try Again later.\n";
                send_to_client(new_socket, msg);
                close(new_socket);
                printf("Connection rejected : Server full\n");
            }
        }
        else if (errno != EWOULDBLOCK && errno != EAGAIN)
        {
            perror("Accept failure\n");
        }
        // listen for actively contacting clients and then accordingly assigning tasks or other procedures 
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            // for active client checking if they are requesting a task,submiting result or exiting the server 
            if (clientList[i].active)
            {
                char buffer[BUFFER] = {0};
                ssize_t bytes_read = recv(clientList[i].socket, buffer, BUFFER - 1, MSG_DONTWAIT);
                if (bytes_read > 0)
                {
                    buffer[bytes_read] = '\0';
                    printf("Recieved from Client %d : %s\n", i, buffer);

                    if (strncmp(buffer, "GET_TASK", 8) == 0)
                    {
                        if (clientList[i].processing)
                        {
                            char *msg = "Already processing task\n";
                            send_to_client(clientList[i].socket, msg);
                        }
                        else
                        {
                            int task_id = find_task();
                            if (task_id >= 0)
                            {
                                TasksList[task_id].assigned = 1;
                                TasksList[task_id].client = i;
                                clientList[i].task_id = task_id;
                                clientList[i].processing = 1;

                                char task_msg[BUFFER];
                                sprintf(task_msg, "Task: %s", TasksList[task_id].task);
                                send_to_client(clientList[i].socket, task_msg);

                                printf("Task %d assigned to client %d : %s\n", task_id, i, TasksList[task_id].task);

                                pid_t pid = fork();
                                if (pid == 0)
                                {
                                    sleep(1);
                                    exit(0);
                                }
                                else if (pid > 0)
                                {
                                    client_pids[i] = pid;
                                    printf("child process spawned for client %d\n", i);
                                }
                                else
                                {
                                    perror("fork failed\n");
                                }
                            }
                            else
                            {
                                char *msg = "No tasks available\n";
                                send_to_client(clientList[i].socket, msg);
                                printf("No task available for client %d\n", i);
                            }
                        }
                    }
                    else if (strncmp(buffer, "RESULT", 6) == 0)
                    {
                        if (clientList[i].processing && clientList[i].task_id >= 0)
                        {
                            int result;
                            sscanf(buffer, "RESULT %d", &result);
                            TasksList[clientList[i].task_id].completed = 1;

                            clientList[i].processing = 0;
                            clientList[i].task_id = -1;

                            printf("Task completed by client %d with results : %d\n", i, result);
                            char *msg = "Result Recieved";
                            send_to_client(clientList[i].socket, msg);
                        }
                        else
                        {
                            char *msg = "Not processing!";
                            send_to_client(clientList[i].socket, msg);
                        }
                    }
                    else if (strncmp(buffer, "exit", 4) == 0)
                    {
                        printf("Client %d disconnecting\n", i);
                        if (clientList[i].processing && clientList[i].task_id >= 0)
                        {
                            TasksList[clientList[i].task_id].assigned = 0;
                            TasksList[clientList[i].task_id].client = -1;
                            printf("Task %d , returned\n", clientList[i].task_id);
                        }

                        close_client(i);
                    }
                    else
                    {
                        char *msg = "Unknown Command \n";
                        send_to_client(clientList[i].socket, msg);
                    }
                }
                // if no message is recieved from the client then we disconnect the client 
                else if (bytes_read == 0)
                {
                    printf("Client %d disconnected\n", i);

                    if (clientList[i].processing && clientList[i].task_id >= 0)
                    {
                        TasksList[clientList[i].task_id].assigned = 0;
                        TasksList[clientList[i].task_id].client = -1;
                        printf("Task %d returned to queue\n", clientList[i].task_id);
                    }

                    close_client(i);
                }
            }
        }
        // checking if all the tasks have been completed in the tasksList
        int all_task_completed = 1;
        for (int i = 0; i < noTasks; i++)
        {
            if (!TasksList[i].completed)
            {
                all_task_completed = 0;
                break;
            }
        }

        if (all_task_completed && noTasks > 0)
        {
            printf("All tasks completed\n");
        }

        usleep(10000);
    }
    // closing the server socket 
    close(server_sock);
    return 0;
}
