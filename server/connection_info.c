#include "connection_info.h"

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include "../aesd-char-driver/aesd_ioctl.h"

void *connection_thread_function(void *thread_arguments)
{
    ConnectionInfo *connection_info = (ConnectionInfo *)thread_arguments;
    FILE *output_file = NULL;

    ssize_t received_bytes = 0;
    memset(connection_info->message_buffer, 0, sizeof(connection_info->message_buffer));
    if (pthread_mutex_lock(connection_info->output_file_mutex) != 0)
    {
        fprintf(stderr, "Failed to lock output file mutex: %s, %d", __FILE__, __LINE__);
        goto output_file_mutex_lock_failed;
    }

#if USE_AESD_CHAR_DEVICE
    output_file = fopen("/dev/aesdchar", "a+");
#else
    output_file = fopen("/var/tmp/aesdsocketdata", "a+");
#endif
    if (output_file == NULL)
    {
        perror("fopen");
        goto fopen_failed;
    }

    bool go_to_beginning = true;

    while (strchr(connection_info->message_buffer, '\n') == NULL)
    {
        received_bytes = recv(connection_info->client_descriptor, connection_info->message_buffer, sizeof(connection_info->message_buffer) - 1, 0);
        if (received_bytes == -1)
        {
            perror("recv");
            goto early_return;
        }

        connection_info->message_buffer[received_bytes] = '\0';

        if (strncmp(connection_info->message_buffer, "AESDCHAR_IOCSEEKTO:", 19) == 0 && strlen(connection_info->message_buffer) == 23)
        {
            int file_descriptor = fileno(output_file);
            AesdSeekTo seek_to = {
                .write_cmd = connection_info->message_buffer[19] - '0',
                .write_cmd_offset = connection_info->message_buffer[21] - '0',
            };

            ioctl(file_descriptor, AESDCHAR_IOCSEEKTO, &seek_to);
            go_to_beginning = false;
        }
        else if (fprintf(output_file, "%s", connection_info->message_buffer) == -1)
        {
            fprintf(stderr, "failed to write to file: %s", connection_info->message_buffer);
            goto early_return;
        }
    }

    if (go_to_beginning)
    {
        fseek(output_file, 0, SEEK_SET);
    }

    memset(connection_info->message_buffer, 0, sizeof(connection_info->message_buffer));
    while (fgets(connection_info->message_buffer, sizeof(connection_info->message_buffer), output_file) != NULL)
    {
        if (sendto(connection_info->client_descriptor, connection_info->message_buffer, strlen(connection_info->message_buffer), MSG_NOSIGNAL, (struct sockaddr *)&connection_info->client_address, connection_info->client_length) == -1)
        {
            perror("sendto");
            goto early_return;
        }
    }

early_return:
    fclose(output_file);
fopen_failed:
    if (pthread_mutex_unlock(connection_info->output_file_mutex))
    {
        perror("pthread_mutex_unlock");
    }
    syslog(LOG_NOTICE, "Closed connection from %s", inet_ntoa(connection_info->client_address.sin_addr));
output_file_mutex_lock_failed:
    atomic_store(&connection_info->thread_complete, true);
    shutdown(connection_info->client_descriptor, SHUT_RDWR);
    close(connection_info->client_descriptor);

    return NULL;
}
