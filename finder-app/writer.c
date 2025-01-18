#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(int argc, const char** argv) {
    if(argc < 3 && argc > 0) {
        syslog(LOG_ERR, "Expected format: %s <File Name> <String to Write>", argv[0]);
        exit(1);
    } else if (argc <= 0) {
        syslog(LOG_ERR, "Number of arguments was <= 0, which shouldn't be possible");
        exit(1);
    }

    FILE* file = fopen(argv[1], "w");
    if(file == NULL) {
        syslog(LOG_ERR, "%s", strerror(errno));
        exit(1);
    }

    fprintf(file, "%s\n", argv[2]);
    fclose(file);

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
}
