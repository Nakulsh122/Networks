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

// consts

#define PORT 6000
#define MAX_TASKS 100
#define BUFFER 1024
#define MAX_CLIENTS 100
#define TASK_SIZE 100

typedef struct
{
    int assigned;
    int completed;
    int client;
    char task[TASK_SIZE];
} task;

typedef struct
{
    int socket;
    int active;
    int processing;
    int task_id;
} client;

task TasksList[MAX_TASKS];
client clientList[MAX_CLIENTS];
int noTasks = 0;
int client_pids[MAX_CLIENTS];

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
    while (fgets(TasksList[noTasks].task, TASK_SIZE, file) && noTasks < MAX_TASKS)
    {
        TasksList[noTasks].assigned = 0;
        TasksList[noTasks].client = -1;
        TasksList[noTasks].completed = 0;
        noTasks++;
    }
    fclose(file);
    printf("All task read and stored\n");
}

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
                {
                    TasksList[i].assigned = 0;
                    TasksList[i].client = -1;
                    printf("Tasks %d returned to the queue\n", clientList[i].task_id);
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

void send_to_client(int socket, char *msg)
{
    send(socket, msg, strlen(msg), 0);
    printf("Message send to %d\n", socket);
}

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
        printf("Usage: %s <tasks_file>\n", argv[0]);
        exit(1);
    }

    read_tasks(argv[1]);
    init_clients();
    struct sigaction sa;
    sa.sa_handler = handle_dead_process;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

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

    int flags = fcntl(server_sock, F_GETFL, 0);
    fcntl(server_sock, F_SETFL, flags | O_NONBLOCK);

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_socket = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);

        if (new_socket > 0)
        {
            printf("New connrction from %s : %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            int client_index = find_client();
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

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
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
                                char *msg = "No task available\n";
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

    close(server_sock);
    return 0;
}
