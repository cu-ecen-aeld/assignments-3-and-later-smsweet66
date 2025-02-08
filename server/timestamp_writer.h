#pragma once

#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct TimestampWriter
{
    FILE *output_file;
    pthread_mutex_t *output_file_mutex;

    _Atomic bool should_close;
} TimestampWriter;

void *timestamp_writer_thread_function(void *thread_arguments);
