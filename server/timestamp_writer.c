#include "timestamp_writer.h"

#include <time.h>
#include <errno.h>

void *timestamp_writer_thread_function(void *thread_arguments)
{
    TimestampWriter *timestamp_writer = (TimestampWriter *)thread_arguments;
    FILE *output_file = NULL;

    while (true)
    {
        struct timespec start = {.tv_sec = 10};

        if (nanosleep(&start, NULL) == -1 && errno == EINTR)
        {
            return NULL;
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

        output_file = fopen("/var/tmp/aesdsocketdata", "a+");
        if (output_file == NULL)
        {
            return NULL;
        }

        if (fprintf(output_file, "timestamp:%s\n", buffer) == -1)
        {
            fprintf(stderr, "writing time to file failed\n");
            fclose(output_file);
            return NULL;
        }

        fclose(output_file);

        if (pthread_mutex_unlock(timestamp_writer->output_file_mutex) != 0)
        {
            perror("pthread_mutex_unlock");
            return NULL;
        }

        if (atomic_load(&timestamp_writer->should_close))
        {
            return NULL;
        }
    }
}
