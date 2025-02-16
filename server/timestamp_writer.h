#pragma once

#include <arpa/inet.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct TimestampWriter
{
    pthread_mutex_t *output_file_mutex;

    atomic_bool should_close;
} TimestampWriter;

typedef struct TimestampWriterThread
{
    TimestampWriter thread_arguments;
    pthread_t thread;
} TimestampWriterThread;

void *timestamp_writer_thread_function(void *thread_arguments);
