#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <syslog.h>
#include <unistd.h>

#include "connection_info.h"
#include "timestamp_writer.h"

int *server_descriptor = NULL;
struct sockaddr_in *server_address = NULL;

pthread_mutex_t *output_file_mutex = NULL;

#if !USE_AESD_CHAR_DEVICE
TimestampWriterThread *timestamp_writer_thread = NULL;
#endif

ConnectionListHead *head = NULL;

pthread_t main_thread = 0;

void handle_incoming_signal(int signal)
{
    // Prevent signal handler from running on child threads;
    if (pthread_self() != main_thread)
    {
        return;
    }

    syslog(LOG_NOTICE, "Caught signal, exiting");
    if (head != NULL)
    {
        while (!SLIST_EMPTY(head))
        {
            ConnectionThread *entry = SLIST_FIRST(head);
            SLIST_REMOVE_HEAD(head, next);
            shutdown(entry->connection_info.client_descriptor, SHUT_RDWR);
            pthread_join(entry->thread, NULL);
            close(entry->connection_info.client_descriptor);
            free(entry);
        }

        free(head);
    }

#if !USE_AESD_CHAR_DEVICE
    if (timestamp_writer_thread != NULL)
    {
        atomic_store(&timestamp_writer_thread->thread_arguments.should_close, true);
        pthread_kill(timestamp_writer_thread->thread, SIGINT);
        pthread_join(timestamp_writer_thread->thread, NULL);
        free(timestamp_writer_thread);
    }
#endif

    if (output_file_mutex != NULL)
    {
        pthread_mutex_destroy(output_file_mutex);
        free(output_file_mutex);
    }

    if (server_descriptor != NULL)
    {
        shutdown(*server_descriptor, SHUT_RDWR);
        close(*server_descriptor);
        free(server_descriptor);
    }

    if (server_address != NULL)
    {
        free(server_address);
    }

    closelog();
#if !USE_AESD_CHAR_DEVICE
    remove("/var/tmp/aesdsocketdata");
#endif

    exit(0);
}

int main(int argc, char *argv[])
{
    main_thread = pthread_self();

    if (argc == 2 && strncmp(argv[1], "-d", 2) != 0)
    {
        fprintf(stderr, "Expected -d argument, got %s", argv[1]);
        goto invalid_arguments;
    }
    else if (argc > 2)
    {
        fprintf(stderr, "Too many arguments: Expected 2, got %d", argc);
        goto invalid_arguments;
    }

    struct sigaction signal_handler = {
        .__sigaction_handler = handle_incoming_signal,
    };

    sigaction(SIGINT, &signal_handler, NULL);
    sigaction(SIGTERM, &signal_handler, NULL);

    server_descriptor = malloc(sizeof(int));
    if (server_descriptor == NULL)
    {
        fprintf(stderr, "Could not malloc!\n");
        goto server_descriptor_malloc_failed;
    }

    *server_descriptor = socket(PF_INET, SOCK_STREAM, 0);
    if (*server_descriptor == -1)
    {
        perror("socket");
        goto socket_failed;
    }

    server_address = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    if (server_address == NULL)
    {
        fprintf(stderr, "Could not malloc!\n");
        goto server_address_malloc_failed;
    }

    memset(server_address, 0, sizeof(struct sockaddr_in));
    server_address->sin_addr.s_addr = htonl(INADDR_ANY);
    server_address->sin_family = AF_INET;
    server_address->sin_port = htons(9000);

    const int enable = 1;
    if (setsockopt(*server_descriptor, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) == -1)
    {
        perror("setsockopt");
        goto set_socket_options_failed;
    }

    if (bind(*server_descriptor, (struct sockaddr *)server_address, sizeof(*server_address)) == -1)
    {
        perror("bind");
        goto bind_failed;
    }

    if (argc == 2)
    {
        fprintf(stderr, "\nCreating daemon\n");
        if (daemon(1, 1) != 0)
        {
            perror("daemon");
            goto daemon_init_failed;
        }
    }

    if (listen(*server_descriptor, 100) == -1)
    {
        perror("listen");
        goto listen_failed;
    }

    output_file_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (output_file_mutex == NULL)
    {
        fprintf(stderr, "Could not malloc!\n");
        goto output_file_mutex_malloc_failed;
    }

    if (pthread_mutex_init(output_file_mutex, NULL) != 0)
    {
        perror("pthread_mutex_init");
        goto output_file_mutex_init_failed;
    }

#if !USE_AESD_CHAR_DEVICE
    timestamp_writer_thread = (TimestampWriterThread *)malloc(sizeof(TimestampWriterThread));
    if (timestamp_writer_thread == NULL)
    {
        perror("malloc");
        goto timestamp_writer_thread_malloc_failed;
    }

    atomic_store(&timestamp_writer_thread->thread_arguments.should_close, false);
    timestamp_writer_thread->thread_arguments.output_file = output_file;
    timestamp_writer_thread->thread_arguments.output_file_mutex = output_file_mutex;
    if (pthread_create(&timestamp_writer_thread->thread, NULL, timestamp_writer_thread_function, (void *)&timestamp_writer_thread->thread_arguments) != 0)
    {
        perror("pthread_create");
        goto timestamp_writer_thread_create_failed;
    }
#endif

    openlog(NULL, 0, LOG_USER);

    head = (ConnectionListHead *)malloc(sizeof(ConnectionListHead));
    if (head == NULL)
    {
        perror("malloc");
        goto connection_list_head_malloc_failed;
    }

    memset(head, 0, sizeof(ConnectionListHead));
    SLIST_INIT(head);
    while (true)
    {
        ConnectionThread *connection_thread = (ConnectionThread *)malloc(sizeof(ConnectionThread));
        if (connection_thread == NULL)
        {
            perror("malloc");
            goto error_in_loop;
        }

        memset(connection_thread, 0, sizeof(ConnectionThread));
        SLIST_INSERT_HEAD(head, connection_thread, next);

        connection_thread->connection_info.client_length = sizeof(connection_thread->connection_info.client_address);
        connection_thread->connection_info.client_descriptor = accept(*server_descriptor, (struct sockaddr *)&connection_thread->connection_info.client_address, &connection_thread->connection_info.client_length);
        if (connection_thread->connection_info.client_descriptor == -1)
        {
            perror("accept");
            free(connection_thread);
            goto error_in_loop;
        }

        syslog(LOG_NOTICE, "Accepted connection from %s", inet_ntoa(connection_thread->connection_info.client_address.sin_addr));
        connection_thread->connection_info.output_file_mutex = output_file_mutex;

        if (pthread_create(&connection_thread->thread, NULL, connection_thread_function, (void *)&connection_thread->connection_info) != 0)
        {
            perror("pthread_create");
            close(connection_thread->connection_info.client_descriptor);
            free(connection_thread);
            goto error_in_loop;
        }

        ConnectionThread *iter = SLIST_FIRST(head);
        ConnectionThread *next_connection = SLIST_NEXT(iter, next);

        // Skips check of list head as it was just created
        while (next_connection != NULL)
        {
            if (atomic_load(&next_connection->connection_info.thread_complete))
            {
                SLIST_REMOVE(head, next_connection, ConnectionThread, next);
                if (pthread_join(iter->thread, NULL))
                {
                    // log error and continue
                    perror("pthread_join");
                }

                free(next_connection);
            }
            else
            {
                iter = next_connection;
            }

            next_connection = SLIST_NEXT(iter, next);
        }
    }

error_in_loop:
    while (!SLIST_EMPTY(head))
    {
        ConnectionThread *entry = SLIST_FIRST(head);
        SLIST_REMOVE_HEAD(head, next);
        shutdown(entry->connection_info.client_descriptor, SHUT_RDWR);
        pthread_join(entry->thread, NULL);
        close(entry->connection_info.client_descriptor);
        free(entry);
    }

    free(head);
connection_list_head_malloc_failed:
#if !USE_AESD_CHAR_DEVICE
    atomic_store(&timestamp_writer_thread->thread_arguments.should_close, true);
    pthread_kill(timestamp_writer_thread->thread, SIGINT);
    pthread_join(timestamp_writer_thread->thread, NULL);
timestamp_writer_thread_create_failed:
    free(timestamp_writer_thread);
timestamp_writer_thread_malloc_failed:
#endif
    pthread_mutex_destroy(output_file_mutex);
    closelog();
output_file_mutex_init_failed:
    free(output_file_mutex);
output_file_mutex_malloc_failed:
listen_failed:
daemon_init_failed:
    shutdown(*server_descriptor, SHUT_RDWR);
    close(*server_descriptor);
bind_failed:
set_socket_options_failed:
    free(server_address);
server_address_malloc_failed:
socket_failed:
    free(server_descriptor);
server_descriptor_malloc_failed:
invalid_arguments:
    return 1;
}
