#include <arpa/inet.h>
#include <signal.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int server_descriptor = 0;
struct sockaddr_in server_address = {0};

int client_descriptor = 0;
struct sockaddr_in client_address = {0};

FILE *output_file = NULL;

void close_file_descriptors(const char *failed_function)
{
    perror(failed_function);
    closelog();
    close(server_descriptor);
    close(client_descriptor);
    if (output_file != NULL)
    {
        fclose(output_file);
    }
}

void handle_incoming_signal(int signal)
{
    syslog(LOG_NOTICE, "Caught signal, exiting");
    close_file_descriptors("Signal Received");
    remove("/var/tmp/aesdsocketdata");
    exit(0);
}

int main(int argc, char *argv[])
{
    if (argc == 2 && strncmp(argv[1], "-d", 2) != 0)
    {
        fprintf(stderr, "Expected -d argument, got %s", argv[1]);
        exit(1);
    }
    else if (argc > 2)
    {
        fprintf(stderr, "Too many arguments: Expected 2, got %d", argc);
        exit(1);
    }

    struct sigaction signal_handler = {
        .__sigaction_handler = handle_incoming_signal,
    };

    sigaction(SIGINT, &signal_handler, NULL);
    sigaction(SIGTERM, &signal_handler, NULL);

    server_descriptor = socket(PF_INET, SOCK_STREAM, 0);
    if (server_descriptor == -1)
    {
        close_file_descriptors("socket");
        exit(1);
    }

    server_address = (struct sockaddr_in){.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY), .sin_port = htons(9000)};
    if (bind(server_descriptor, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
    {
        close_file_descriptors("bind");
        exit(1);
    }

    if (argc == 2)
    {
        switch (fork())
        {
        case -1:
            close_file_descriptors("fork");
            exit(1);
        case 0:
            break;
        default:
            exit(0);
        }

        if (setsid() == -1)
        {
            close_file_descriptors("setsid");
            exit(1);
        }

        switch (fork())
        {
        case -1:
            close_file_descriptors("fork");
            exit(1);
        case 0:
            break;
        default:
            exit(0);
        }
    }

    if (listen(server_descriptor, 5) == -1)
    {
        close_file_descriptors("listen");
        exit(1);
    }

    openlog(NULL, 0, LOG_USER);

    while (1)
    {
        socklen_t client_length = sizeof(client_address);
        memset(&client_address, 0, sizeof(client_address));
        client_descriptor = accept(server_descriptor, (struct sockaddr *)&client_address, &client_length);
        if (client_descriptor == -1)
        {
            close_file_descriptors("accept");
            exit(1);
        }

        syslog(LOG_NOTICE, "Accepted connection from %s", inet_ntoa(client_address.sin_addr));
        FILE *file = fopen("/var/tmp/aesdsocketdata", "w+");
        if (file == NULL)
        {
            close_file_descriptors("fopen");
            exit(1);
        }

        ssize_t received_bytes = 0;
        char buffer[10000] = {0};
        while (1)
        {
            received_bytes = recv(client_descriptor, buffer, sizeof(buffer) - 1, 0);
            if (received_bytes == -1)
            {
                close_file_descriptors("recv");
                exit(1);
            }
            else if (received_bytes == 0)
            {
                break;
            }

            buffer[received_bytes] = '\0';
            printf("Received String: %s\n", buffer);
            if (fprintf(file, "%s", buffer) == -1)
            {
                close_file_descriptors("fprintf");
                exit(1);
            }
        }

        fclose(file);
        file = fopen("/var/tmp/aesdsocketdata", "r");
        if (file == NULL)
        {
            close_file_descriptors("fopen");
            exit(1);
        }

        while (fgets(buffer, sizeof(buffer), file) != NULL)
        {
            printf("Sending data: %s\n", buffer);
            if (send(client_descriptor, buffer, strlen(buffer), 0) == -1)
            {
                close_file_descriptors("sendto");
                exit(1);
            }
        }

        syslog(LOG_NOTICE, "Closed connection from %s", inet_ntoa(client_address.sin_addr));
    }

    closelog();
    return 0;
}
