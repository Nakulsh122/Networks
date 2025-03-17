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
#include <errno.h>

#define PORT 6000
#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"

// calculate function for the result of the task assigned
int calculate(int a, int b, char op)
{
    switch (op)
    {
    case '+':
        return a + b;
    case '-':
        return a - b;
    case '*':
        return a * b;
    case '/':
        if (b == 0)
        {
            fprintf(stderr, "Error: Division by zero\n");
            return 0;
        }
        return a / b;
    default:
        fprintf(stderr, "Error: Unknown operator '%c'\n", op);
        return 0;
    }
}
// the helping function so that the calculate only gets the operands in the input
int solve_task(const char *task)
{
    int a, b;
    char op;

    if (sscanf(task, "Task: %d %c %d", &a, &op, &b) != 3)
    {
        fprintf(stderr, "Error parsing task: %s\n", task);
        return -1;
    }

    printf("Parsed task: %d %c %d\n", a, op, b);
    return calculate(a, b, op);
}

int main(int argc, char *argv[])
{
    // socket initialisation for the server socket
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    int task_count = 0;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address/ Address not supported");
        return -1;
    }
    // connection to the server using the socket created
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection Failed");
        return -1;
    }

    printf("Connected to Task Queue Server\n");
    // assigning the no. of tasks that the client will take at max
    int max_tasks = (argc > 1) ? atoi(argv[1]) : 5;

    while (task_count < max_tasks)
    {
        printf("Requesting a task...\n");
        send(sock, "GET_TASK", strlen("GET_TASK"), 0);

        sleep(1);
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_read = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        // receiving the message from the server : can be no task availabe or a task
        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0';
            printf("Server response: %s", buffer);
            // checking if no task is available
            if (strncmp(buffer, "No tasks available", 18) == 0)
            {
                printf("No more tasks available. Exiting.\n");
                continue;
            }

            if (strncmp(buffer, "Task:", 5) == 0)
            {
                int result = solve_task(buffer); //solving the recieved task 
                if (result != -1)
                {
                    // sending the result message to the server
                    char result_msg[BUFFER_SIZE];
                    snprintf(result_msg, BUFFER_SIZE, "RESULT %d", result);
                    printf("Sending result: %s\n", result_msg);
                    send(sock, result_msg, strlen(result_msg), 0);
                    // sleeping to wait for the servers reply
                    sleep(1);
                    memset(buffer, 0, BUFFER_SIZE);
                    bytes_read = recv(sock, buffer, BUFFER_SIZE - 1, 0);
                    if (bytes_read > 0)
                    {
                        buffer[bytes_read] = '\0';
                        printf("Server response: %s", buffer);
                    }

                    task_count++;
                }
            }

            // if no bytes are read from the server then we close the connection
        }
        else if (bytes_read == 0)
        {
            printf("Server disconnected\n");
            break;
        }
        else
        {
            perror("recv failed");
            break;
        }
    }

    sleep(2);
    // exit message to the server when no task is recieved
    printf("Sending exit message to server\n");
    send(sock, "exit", strlen("exit"), 0);
    // closing the server connecting socket
    close(sock);
    printf("Worker client terminated after processing %d tasks\n", task_count);

    return 0;
}