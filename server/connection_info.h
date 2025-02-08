#pragma once

#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct ConnectionInfo
{
    FILE *output_file;
    pthread_mutex_t *output_file_mutex;

    int client_descriptor;
    struct sockaddr_in client_address;
    socklen_t client_length;
    char message_buffer[500];

    _Atomic bool thread_complete;
} ConnectionInfo;

void *connection_thread_function(void *thread_arguments);
