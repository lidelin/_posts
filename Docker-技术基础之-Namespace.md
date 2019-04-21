---
title: Docker 技术基础之 Namespace
date: 2019-04-20 19:38:28
tags: docker,linux
---

看到 Namespace 这个关键字，很多人可能会以为是 C++ 或者 PHP 等编程语言中的 Namespace。其实不然，这里说 Namespace，是 Linux 的 Namespace，容器技术的基础之一。

# 简介

Namespace 是 Linux Kernel 的一个功能，是对整个系统资源的一种抽象，主要用于资源隔离。在同一 namespace 下的所有进程有它们自己独立隔离的系统资源，系统资源的变化只对该 namespace 下的进程可见，对其他进程不可见。

<!--more-->

# Namespace 类型

| Namespace | 系统参数 | 隔离内容 |
| ------ | ------ | ------ |
| IPC | CLONE_NEWIPC | System V IPC, POSIX 消息队列 |
| Network | CLONE_NEWNET | 网络设备、网络栈、端口等 |
| Mount | CLONE_NEWNS | 挂载点 |
| PID | CLONE_NEWPID | 进程 ID |
| User | CLONE_NEWUSER | 用户和用户组 IDs |
| UTS | CLONE_NEWUTS | 主机名和网络信息域名 |

> PS: System V 引入了三种高级进程间的通信机制：消息队列、共享内存和信号量

# Namespace 在 /proc 目录的内容

每个进程都会有一个 /proc/[pid]/ns/ 的子目录，在该目录下会有每个 Namespace 的入口：

```bash
root@iZwz9a9kyixoqw1qrxhvpqZ:~# ls /proc/$$/ns -l
total 0
lrwxrwxrwx 1 root root 0 Apr 20 21:50 ipc -> ipc:[4026531839]
lrwxrwxrwx 1 root root 0 Apr 20 21:50 mnt -> mnt:[4026531840]
lrwxrwxrwx 1 root root 0 Apr 20 21:50 net -> net:[4026531957]
lrwxrwxrwx 1 root root 0 Apr 20 21:50 pid -> pid:[4026531836]
lrwxrwxrwx 1 root root 0 Apr 20 21:50 user -> user:[4026531837]
lrwxrwxrwx 1 root root 0 Apr 20 21:50 uts -> uts:[4026531838]
```

每个文件都是对应 namespace 的文件描述符，方括号里面的值是 namespace 的 inode，如果两个进程所在的 namespace 一样，那么它们列出来的 inode 是一样的；反之亦然。如果某个 namespace 中没有进程了，它会被自动删除，不需要手动删除。但是有个例外，如果 namespace 对应的文件某个应用程序打开，那么该 namespace 是不会被删除的，这个特性可以让我们保持住某个 namespace，以便后面往里面添加进程。

# 牛刀小试

## 三个系统调用

- clone

创建一个新进程，并设置对应的 namespace

```c
/* Prototype for the glibc wrapper function */

#include <sched.h>

int clone(int (*fn)(void *), void *child_stack,
          int flags, void *arg, ...
          /* pid_t *ptid, struct user_desc *tls, pid_t *ctid */ );
```

参数说明：
1. *fn: 子进程启动时执行的函数 
2. *child_stack: 子进程执行时使用的栈内存
3. flag: 子进程启动的配置信息，包括信号、namespace 等
4. *arg: 传给子进程的参数

- setns

把一个进程加到一个已存在的 namespace

- unshare

把一个进程移到一个新的 namespace

## 实践

### UTS Namespace

UTS Namespace 可以让一个系统中的不同进程有不同的主机名，在网络上被视为一个独立的节点。

```c
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
```

运行结果

```bash
root@iZwz9a9kyixoqw1qrxhvpqZ:~# hostname
iZwz9a9kyixoqw1qrxhvpqZ
root@iZwz9a9kyixoqw1qrxhvpqZ:~# ./container
Parent pid: [14532]
Inside child process!
# hostname
child
#
```

可以看到，在子进程启动的 shell 中，hostname 已经变成了 "child" 了

### PID Namespace

PID Namespace 会对进程 PID 重新标号，即两个不同 namespace 下的进程可以有同一个 PID。

我们先来看看子进程和父进程在同一 namespace 的情况

```c
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
```
运行结果
```bash
root@iZwz9a9kyixoqw1qrxhvpqZ:~# ./a.out
Parent pid: [9434]
Child pid: [9435]
Parent - child process stopped!
```

可以看到父进程的 PID 是 9434，子进程的 PID 是 9435

我们再看看子进程在一个新的 namespace 的情况。
小作修改，在调用 clone 的第三个参数加上 CLONE_NEWPID

```c

...

pid_t child_pid = clone(child_main,
                            child_stack + sizeof(child_stack), /* Points to start of downwardly growing stack */
                            CLONE_NEWPID | SIGCHLD,
                            NULL);

...

```

运行结果

```bash
root@iZwz9a9kyixoqw1qrxhvpqZ:~# ./a.out
Parent pid: [8984]
Child pid: [1]
Parent - child process stopped!
```

可以看到父进程的 PID 是 3725，子进程的 PID 是 1


实践中演示了 UTS 和 PID Namespace，剩下的 IPC、Network、Mount、User Namespace 就不一一演示了。

# 总结

本文主要介绍了以下内容：
1. 简单介绍了 Linux 的 Namespace
2. 列举了 Namespace 的类型
3. 简单介绍了 Namespace 在 /proc 目录的内容
4. 介绍了和 namespace 相关的系统调用
5. 实践了 PID Namespace


# 参考

[1. Wikipedia Linux Namespaces](https://en.wikipedia.org/wiki/Linux_namespaces)
[2. DOCKER基础技术：LINUX NAMESPACE（上）](https://coolshell.cn/articles/17010.html)
[3. DOCKER基础技术：LINUX NAMESPACE（下）](https://coolshell.cn/articles/17029.html)
[4. Docker 背后的内核知识——Namespace 资源隔离](https://www.infoq.cn/article/docker-kernel-knowledge-namespace-resource-isolation)
[5. Namespaces in operation, part 1: namespaces overview](https://lwn.net/Articles/531114/)
[6. docker 容器基础技术：linux namespace 简介](https://cizixs.com/2017/08/29/linux-namespace/)
