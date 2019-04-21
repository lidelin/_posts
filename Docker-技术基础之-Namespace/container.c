#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

static char child_stack[STACK_SIZE];

char *const child_args[] = {
    "/bin/sh",
    NULL};

int child_main(void *arg)
{
    printf("Inside child process!\n");

    sethostname("child", 6);

    /* 直接执行一个shell，以便我们观察这个进程空间里的资源是否被隔离了 */
    execv(child_args[0], child_args);

    return 0;
}

int main()
{
    pid_t pid = getpid();
    printf("Parent pid: [%d]\n", pid);

    pid_t child_pid = clone(child_main,
                            child_stack + sizeof(child_stack), /* Points to start of downwardly growing stack */
                            CLONE_NEWUTS | SIGCHLD,
                            NULL);

    waitpid(child_pid, NULL, 0);

    printf("Parent - child process stopped!\n");

    return 0;
}