#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

static char child_stack[STACK_SIZE];

int child_main(void *arg)
{
    pid_t pid = getpid();

    printf("Child pid: [%d]\n", pid);

    return 0;
}

int main()
{
    pid_t pid = getpid();
    printf("Parent pid: [%d]\n", pid);

    pid_t child_pid = clone(child_main,
                            child_stack + sizeof(child_stack), /* Points to start of downwardly growing stack */
                            SIGCHLD,
                            NULL);

    waitpid(child_pid, NULL, 0);

    printf("Parent - child process stopped!\n");

    return 0;
}