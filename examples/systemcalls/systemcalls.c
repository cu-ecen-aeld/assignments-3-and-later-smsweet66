#include "systemcalls.h"


#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    if(system(cmd) != 0) {
        return false;
    }

    return true;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    va_end(args);

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    int childID;
    fflush(stdout);
    switch(childID = fork()) {
    case -1:
        perror("Fork Failed");
        return false;
    case 0:
        printf("Executing command in new process\n");
        execv(command[0], command);
        perror("Execv Failed");
        exit(1);
    default:
        break;
    }

    int status;
    if(waitpid(childID, &status, 0) == -1) {
        perror("Waitpid Failed");
        return false;
    }

    if(WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        printf("Exit Code: %d\n", exit_code);
        if(exit_code == 0) {
            return true;
        } else {
            return false;
        }
    }


    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    va_end(args);

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

    int childID;
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    fflush(stdout);
    switch(childID = fork()) {
    case -1:
        close(fd);
        perror("Fork Failed");
        return false;
    case 0:
        if(dup2(fd, 1) < 0) {
            perror("Dup2 Failed");
            exit(1);
        }

        close(fd);
        execv(command[0], command);
        perror("Execv Failed");
        exit(1);
    default:
        close(fd);
        break;
    }

    int status;
    if(waitpid(childID, &status, 0) == -1) {
        perror("Waitpid Failed");
        return false;
    }

    if(WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        printf("Exit code: %d\n", exit_code);
        if(exit_code == 0) {
            return true;
        } else {
            printf("Returning false\n");
            return false;
        }
    }


    return true;
}
