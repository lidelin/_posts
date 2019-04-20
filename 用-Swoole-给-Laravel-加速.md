---
title: 用 Swoole 给 Laravel 加速
date: 2018-09-23 16:42:16
tags:
    - swoole
    - laravel
---

# 前言

原文：https://laravel-news.com/laravel-swoole

之前在 [Swoole 初探](https://lidelin.github.io/2018/08/26/Swoole-%E5%88%9D%E6%8E%A2/) 对 swoole 有了个初步认识，今天我们来看看 swoole 和 laravel 结合会给 laravel 带来什么样的变化。
我们今天用的包是 [swooletw/laravel-swoole](https://github.com/swooletw/laravel-swoole)。

<!--more-->

# 为什么要让 Laravel 跑在 Swoole 上

![](Vjtm7I9-1.png)

上面这幅图描述了 PHP 的生命周期。从上图可以看出，每次执行 PHP 脚本时，PHP 都要初始化基础模块、运行 Zend 引擎和把 PHP 脚本编译成 OpCodes。
运行环境在每次处理完一个请求后都会被销毁，所以上图的流程每次处理请求的时候都要重头执行一次。也就是说，每次处理请求时，都要浪费大量的资源来初始化环境。
再想想 Laravel，每处理一个请求要加载多少文件？这些重复的流程浪费了大量的 I/O 。
利用 Swoole，可以 PHP 程序常驻内存，不需要每次处理请求都要重新初始化 PHP 环境，这样来给 Laravel 加速。

# 安装
1. 安装 swoole
    可以参考
2. 安装 swooletw/laravel-swoole
    ```bash
    composer require swooletw/laravel-swoole
    ```

# Nginx 配置
```
map $http_upgrade $connection_upgrade {
    default upgrade;
    ''      close;
}
server {
    listen 80;
    server_name your.domain.com;
    root /path/to/laravel/public;
    index index.php;

    location = /index.php {
        try_files /not_exists @swoole;
    }

    location / {
        try_files $uri $uri/ @swoole;
    }

    location @swoole {
        set $suffix "";

        if ($uri = /index.php) {
            set $suffix "/";
        }

        proxy_set_header Host $http_host;
        proxy_set_header Scheme $scheme;
        proxy_set_header SERVER_PORT $server_port;
        proxy_set_header REMOTE_ADDR $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection $connection_upgrade;

        proxy_pass http://127.0.0.1:1215$suffix;
    }
}
```

# 运行
```bash
php artisan swoole:http start
```

# 测试

## 坑
一开始直接测试 Laravel 开箱的欢迎页，多测几次，发现并发越来越低。
原因是压测的时候，每次访问都会生成新的 session 文件。
后来改成压测一个返回 { "hello": "world" } 的请求，压测结果就正常了。

## Benchmark

- 未使用 swoole 
    ```
    wrk -t4 -c100 http://laravel.local/api/hello

    Running 10s test @ http://laravel.local/api/hello
      4 threads and 100 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency   377.34ms   58.73ms 572.24ms   70.54%
        Req/Sec    65.96     29.00   170.00     69.90%
      2600 requests in 10.01s, 24.77MB read
      Non-2xx or 3xx responses: 2455
    Requests/sec:    259.70
    Transfer/sec:      2.47MB
    ```
    
- 使用 swoole
    ```
    wrk -t4 -c100 http://laravel-swoole.local/api/hello
    
    Running 10s test @ http://laravel-swoole.local/api/hello
      4 threads and 100 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency   117.40ms   23.28ms 237.19ms   92.03%
        Req/Sec   212.69     34.59   252.00     83.00%
      8478 requests in 10.01s, 84.90MB read
      Non-2xx or 3xx responses: 8415
    Requests/sec:    846.75
    Transfer/sec:      8.48MB
    ```



