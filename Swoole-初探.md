---
title: Swoole 初探
date: 2018-08-26 18:01:40
tags: swoole
---

听说 swoole 很久了，一直没有用过，今天心血来潮跑了个 echo server，在此记录一下。

# 简介

Swoole 是一个 PHP 扩展，可以使 PHP 开发人员可以编写高性能的异步并发 TCP、UDP、Unix Socket、HTTP，WebSocket 服务。更详细的介绍可以到[官网](https://www.swoole.com/)看一下。

# Echo 服务器

echo 服务器，服务端收到什么，就给客户端发送什么。

## 安装 Swoole
```bash
pecl install swoole
```

## 服务端代码
```php
<?php

class Server
{
    private $server;

    public function __construct()
    {
        // 创建一个异步 Server 对象
        $this->server = new swoole_server("0.0.0.0", 9501);

        // 设置 swoole_server 运行时的各项参数
        // worker_num: 启动的 Worker 进程数
        // daemonize: 守护进程化。设置daemonize => 1时，程序将转入后台作为守护进程运行。
        $this->server->set(array(
            'worker_num' => 8,
            'daemonize' => false,
        ));

        // 注册 Server 的事件回调函数

        // Server 启动在主进程的主线程回调此函数
        $this->server->on('Start', array($this, 'onStart'));

        // 有新的连接进入时，在 worker 进程中回调
        $this->server->on('Connect', array($this, 'onConnect'));

        // 接收到数据时回调此函数，发生在worker进程中
        $this->server->on('Receive', array($this, 'onReceive'));

        // TCP 客户端连接关闭后，在 worker 进程中回调此函数
        $this->server->on('Close', array($this, 'onClose'));

        // 启动 Server
        $this->server->start();
    }

    public function onStart($server)
    {
        echo "Start\n";
    }

    public function onConnect($server, $fd, $reactorId)
    {
        // 向客户端发送数据
        $server->send($fd, "Hello {$fd}!");
    }

    public function onReceive(swoole_server $server, $fd, $reactorId, $data)
    {
        echo "Get Message From Client {$fd}:{$data}\n";

        // 向客户端发送数据
        $server->send($fd, $data);
    }

    public function onClose($server, $fd, $reactorId)
    {
        echo "Client {$fd} close connection\n";
    }
}

$server = new Server();


```

构造函数注册的回调事件，可以在[这里](https://wiki.swoole.com/wiki/page/41.html)查到。



## 客户端代码
```php
<?php

class Client
{
    private $client;

    public function __construct()
    {
        // 创建一个 TCP Socket 客户端
        $this->client = new swoole_client(SWOOLE_SOCK_TCP);
    }

    public function connect()
    {
        // 连接到远程服务器
        if (!$this->client->connect("127.0.0.1", 9501, 1)) {
            echo "Error: {$this->client->errMsg}[{$this->client->errCode}]\n";
        }

        // 输出 "请输入消息 Please input msg：" 到控制台
        fwrite(STDOUT, "请输入消息 Please input msg：");

        // 从控制台读取输入
        $msg = trim(fgets(STDIN));

        // 发送数据到远程服务器
        $this->client->send($msg);

        // 从服务器端接收数据
        $message = $this->client->recv();
        echo "Get Message From Server:{$message}\n";
    }
}

$client = new Client();
$client->connect();

```

## 测试步骤

- 创建一个 Server.php，并输入服务端的代码
- 创建一个 Client.php，并输入客户端的代码
- 在一个控制台执行 php Server.php 运行服务端
- 在另一个控制台执行 php Client.php 运行客户端，并输入任何字符

运行截图如下：

服务端
![](server.png)

客户端
![](client.png)


# Swoole 相关知识

## 进程 / 线程结构

Swoole 是一个多进程模式的框架（可以类比Nginx的进程模型），当启动一个 Swoole 应用时，一共会创建 2 + n + m 个进程，其中 n 为 Worker 进程数，m 为 TaskWorker 进程数，2 为一个 Master 进程和一个 Manager 进程。Reactor 线程实际运行 epoll 实例，用于 accept 客户端连接以及接收客户端数据；Manager 进程为管理进程，该进程的作用是创建、管理所有的 Worker 进程和 TaskWorker 进程。

![](swoole.jpg)

## Reactor、Worker、TaskWorker

### Reactor
- 负责维护客户端 TCP 连接、处理网络 IO、处理协议、收发数据
- 完全是异步非阻塞的模式
- 除 Start / Shudown 事件回调外，不执行任何 PHP 代码
- 将 TCP 客户端发来的数据缓冲、拼接、拆分成完整的一个请求数据包
- Reactor 以多线程的方式运行

### Worker
- 接受由 Reactor 线程投递的请求数据包，并执行 PHP 回调函数处理数据
- 生成响应数据并发给 Reactor 线程，由 Reactor 线程发送给 TCP 客户端
- 可以是异步非阻塞模式，也可以是同步阻塞模式
- Worker 以多进程的方式运行

### TaskWorker
- 接受由 Worker 进程通过 swoole_server->task/taskwait 方法投递的任务
- 处理任务，并将结果数据返回 (swoole_server->finish) 给 Worker 进程
- 完全是同步阻塞模式
- TaskWorker 以多进程的方式运行

### 关系
可以理解为 Reactor 就是 nginx，Worker 就是 php-fpm 。Reactor 线程异步并行地处理网络请求，然后再转发给 Worker 进程中去处理。Reactor 和 Worker 间通过 UnixSocket 进行通信。

一个更通俗的比喻，假设 Server 就是一个工厂，那 Reactor 就是销售，接受客户订单。而 Worker 就是工人，当销售接到订单后，Worker 去工作生产出客户要的东西。而 TaskWorker 可以理解为行政人员，可以帮助 Worker 干些杂事，让 Worker 专心工作。


