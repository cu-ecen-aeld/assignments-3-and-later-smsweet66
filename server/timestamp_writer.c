#include "timestamp_writer.h"

#include <time.h>

void *timestamp_writer_thread_function(void *thread_arguments)
{
    TimestampWriter *timestamp_writer = (TimestampWriter *)thread_arguments;

    while (true)
    {
        struct timespec start = {0};
        clock_gettime(CLOCK_MONOTONIC, &start);

        struct timespec now = {0};
        for (clock_gettime(CLOCK_MONOTONIC, &now); now.tv_sec - start.tv_sec < 10; clock_gettime(CLOCK_MONOTONIC, &now))
        {
            if (atomic_load(&timestamp_writer->should_close))
            {
                return NULL;
            }
        }

        char buffer[80] = {0};
        time_t current_time;
        struct tm *temp_time;

        current_time = time(NULL);
        temp_time = localtime(&current_time);
        if (temp_time == NULL)
        {
            perror("localtime");
            return NULL;
        }

        if (strftime(buffer, sizeof(buffer), "%a, %d %b %Y %T %z", temp_time) == 0)
        {
            fprintf(stderr, "strftime returned 0\n");
            return NULL;
        }

        if (pthread_mutex_lock(timestamp_writer->output_file_mutex) != 0)
        {
            fprintf(stderr, "Failed to lock output file mutex: %s, %d", __FILE__, __LINE__);
            return NULL;
        }

        if (fprintf(timestamp_writer->output_file, "timestamp:%s\n", buffer) == -1)
        {
            fprintf(stderr, "writing time to file failed\n");
            return NULL;
        }

        if (pthread_mutex_unlock(timestamp_writer->output_file_mutex) != 0)
        {
            perror("pthread_mutex_unlock");
            return NULL;
        }
    }
}
