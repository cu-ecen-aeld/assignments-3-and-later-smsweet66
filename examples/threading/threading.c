#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data*) thread_param;

    thread_func_args->thread_complete_success = true;

    struct timespec remaining = thread_func_args->wait_to_obtain;

    if(nanosleep(&thread_func_args->wait_to_obtain, &remaining) != 0) {
        thread_func_args->thread_complete_success = false;
    }

    if(pthread_mutex_lock(thread_func_args->thread_data_mutex) != 0) {
        thread_func_args->thread_complete_success = false;
    }

    remaining = thread_func_args->wait_to_release;
    if(nanosleep(&thread_func_args->wait_to_release, &remaining) != 0) {
        thread_func_args->thread_complete_success = false;
    }

    if(pthread_mutex_unlock(thread_func_args->thread_data_mutex) != 0) {
        thread_func_args->thread_complete_success = false;
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    struct thread_data* thread_func_args = (struct thread_data*) malloc(sizeof(struct thread_data));
    if(thread_func_args == NULL) {
        return false;
    }

    thread_func_args->thread_data_mutex = mutex;
    thread_func_args->thread_complete_success = true;

    thread_func_args->wait_to_obtain.tv_sec = wait_to_obtain_ms/1000;
    thread_func_args->wait_to_obtain.tv_nsec = (wait_to_obtain_ms%1000) * 1000000;

    thread_func_args->wait_to_release.tv_sec = wait_to_release_ms/1000;
    thread_func_args->wait_to_release.tv_nsec = (wait_to_release_ms%1000) * 1000000;

    return pthread_create(thread, NULL, threadfunc, thread_func_args) == 0;
}
