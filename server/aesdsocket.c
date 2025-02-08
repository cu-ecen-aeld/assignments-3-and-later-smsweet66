#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <syslog.h>
#include <unistd.h>

#include "connection_info.h"
#include "timestamp_writer.h"

typedef struct ConnectionThread
{
    ConnectionInfo connection_info;
    pthread_t thread;
    SLIST_ENTRY(ConnectionThread)
    next;
} ConnectionThread;

typedef SLIST_HEAD(ConnectinListHead, ConnectionThread) ConnectinListHead;

int *server_descriptor = NULL;
struct sockaddr_in *server_address = NULL;

FILE *output_file = NULL;
pthread_mutex_t *output_file_mutex = NULL;

ConnectinListHead *head = NULL;

TimestampWriter timestamp_writer = {0};
pthread_t timestamp_writer_thread = 0;

void handle_incoming_signal(int signal)
{
    syslog(LOG_NOTICE, "Caught signal, exiting");
    printf("Caught signal, exiting\n");
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

    timestamp_writer.should_close = true;
    pthread_join(timestamp_writer_thread, NULL);

    if (output_file != NULL)
    {
        fclose(output_file);
    }

    if (output_file_mutex != NULL)
    {
        pthread_mutex_destroy(output_file_mutex);
        free(output_file_mutex);
    }

    if (server_descriptor != NULL)
    {
        close(*server_descriptor);
        free(server_descriptor);
    }

    if (server_address != NULL)
    {
        free(server_address);
    }

    closelog();
    remove("/var/tmp/aesdsocketdata");
    exit(0);
}

int main(int argc, char *argv[])
{
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

    if (bind(*server_descriptor, (struct sockaddr *)server_address, sizeof(*server_address)) == -1)
    {
        perror("bind");
        goto bind_failed;
    }

    if (argc == 2)
    {
        switch (fork())
        {
        case -1:
            perror("fork");
            goto daemon_init_failed;
        case 0:
            break;
        default:
            exit(0);
        }

        if (setsid() == -1)
        {
            perror("setsid");
            goto daemon_init_failed;
        }

        switch (fork())
        {
        case -1:
            perror("fork");
            goto daemon_init_failed;
        case 0:
            break;
        default:
            exit(0);
        }
    }

    if (listen(*server_descriptor, 5) == -1)
    {
        perror("listen");
        goto listen_failed;
    }

    output_file = fopen("/var/tmp/aesdsocketdata", "w+");
    if (output_file == NULL)
    {
        perror("fopen");
        goto fopen_failed;
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

    timestamp_writer.output_file = output_file;
    timestamp_writer.output_file_mutex = output_file_mutex;
    if (pthread_create(&timestamp_writer_thread, NULL, timestamp_writer_thread_function, &timestamp_writer) != 0)
    {
        perror("pthread_create");
        goto timestamp_writer_thread_create_failed;
    }

    openlog(NULL, 0, LOG_USER);

    head = (ConnectinListHead *)malloc(sizeof(ConnectinListHead));
    if (head == NULL)
    {
        perror("malloc");
        goto connection_list_head_malloc_failed;
    }

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

        connection_thread->connection_info.client_length = sizeof(connection_thread->connection_info.client_address);
        connection_thread->connection_info.client_descriptor = accept(*server_descriptor, (struct sockaddr *)&connection_thread->connection_info.client_address, &connection_thread->connection_info.client_length);
        if (connection_thread->connection_info.client_descriptor == -1)
        {
            perror("accept");
            free(connection_thread);
            goto error_in_loop;
        }

        syslog(LOG_NOTICE, "Accepted connection from %s", inet_ntoa(connection_thread->connection_info.client_address.sin_addr));
        connection_thread->connection_info.output_file = output_file;
        connection_thread->connection_info.output_file_mutex = output_file_mutex;

        if (pthread_create(&connection_thread->thread, NULL, connection_thread_function, (void *)&connection_thread->connection_info) != 0)
        {
            perror("pthread_create");
            close(connection_thread->connection_info.client_descriptor);
            free(connection_thread);
            goto error_in_loop;
        }

        SLIST_INSERT_HEAD(head, connection_thread, next);

        for (ConnectionThread *iter = SLIST_FIRST(head); iter != NULL; iter = SLIST_NEXT(iter, next))
        {
            if (iter->connection_info.thread_complete)
            {
                SLIST_REMOVE(head, iter, ConnectionThread, next);
                if (pthread_join(iter->thread, NULL))
                {
                    // log error and continue
                    perror("pthread_join");
                }

                if (close(iter->connection_info.client_descriptor))
                {
                    // log error and continue
                    perror("close");
                }

                free(iter);
            }
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
    timestamp_writer.should_close = true;
    pthread_join(timestamp_writer_thread, NULL);
timestamp_writer_thread_create_failed:
    pthread_mutex_destroy(output_file_mutex);
    closelog();
output_file_mutex_init_failed:
    free(output_file_mutex);
output_file_mutex_malloc_failed:
    fclose(output_file);
fopen_failed:
listen_failed:
daemon_init_failed:
bind_failed:
    free(server_address);
server_address_malloc_failed:
socket_failed:
    free(server_descriptor);
server_descriptor_malloc_failed:
invalid_arguments:
    return 1;
}
