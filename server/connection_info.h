#pragma once

#include <arpa/inet.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/queue.h>

#define USE_AESD_CHAR_DEVICE 1

typedef struct ConnectionInfo
{
    pthread_mutex_t *output_file_mutex;

    int client_descriptor;
    struct sockaddr_in client_address;
    socklen_t client_length;
    char message_buffer[500];

    atomic_bool thread_complete;
} ConnectionInfo;

typedef struct ConnectionThread
{
    ConnectionInfo connection_info;
    pthread_t thread;
    SLIST_ENTRY(ConnectionThread)
    next;
} ConnectionThread;

typedef SLIST_HEAD(ConnectionListHead, ConnectionThread) ConnectionListHead;

void *connection_thread_function(void *thread_arguments);
