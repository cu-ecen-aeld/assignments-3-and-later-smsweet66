#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(int argc, const char** argv) {
    openlog(NULL, 0, LOG_USER);

    if(argc < 3 && argc > 0) {
        syslog(LOG_ERR, "Expected format: %s <File Name> <String to Write>", argv[0]);
        closelog();
        exit(1);
    } else if (argc <= 0) {
        syslog(LOG_ERR, "Number of arguments was <= 0, which shouldn't be possible");
        closelog();
        exit(1);
    }

    FILE* file = fopen(argv[1], "w");
    if(file == NULL) {
        syslog(LOG_ERR, "Error opening file %s: %s", argv[0], strerror(errno));
        closelog();
        exit(1);
    }

    if(fprintf(file, "%s\n", argv[2]) < 0) {
        syslog(LOG_ERR, "Error writing value %s to file %s: %s", argv[1], argv[0], strerror(errno));
        closelog();
        exit(1);
    }

    if(fclose(file) == EOF) {
        syslog(LOG_ERR, "Error closing file %s: %s", argv[0], strerror(errno));
        closelog();
        exit(1);
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    closelog();
}
