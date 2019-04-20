---
title: (译) 用 Nginx 实现 Http 负载均衡器
date: 2018-07-21 11:17:12
tags: nginx
---

原文：[Using nginx as HTTP load balancer](http://nginx.org/en/docs/http/load_balancing.html)

# 前言
负载均衡，是用来优化资源使用、使吞吐率最大化、降低延时和保证系统容错的一种通用技术。nginx 可以作为一个非常高效的 HTTP 负载均衡器，将流量分配给多个应用服务器，同时也可以提高 Web 应用程序的性能、可扩展性和可靠性。

<!--more-->

# 负载均衡机制
nginx 支持以下几种负载均衡机制：
- 轮询调度 —— 请求以轮询调度的方式被分发到服务器
- 最少连接 —— 下个请求被分发到连接活跃数最少的服务器
- IP 地址哈希 —— 使用 IP 地址的哈希值来决定把请求分发到哪个服务器

# 默认的负载均衡配置
nginx 最简单的负载均衡配置如下：
```
http {
    upstream myapp1 {
        server srv1.example.com;
        server srv2.example.com;
        server srv3.example.com;
    }

    server {
        listen 80;

        location / {
            proxy_pass http://myapp1;
        }
    }
}
```
在上面的例子中，有 3 个同样的程序实例运行在 srv1-srv3。当没有具体配置负载均衡机制的时候，默认为轮询调度算法。所有请求被代理到 myapp1 服务器组，然后 nginx 使用负载均衡的方式来分发请求。
nginx 为 HTTP、HTTPS、FastCGI、uwsgi、SCGI、memcached 和 gRPC 实现了反向代理负载均衡。
如果把负载均衡从 HTTP 改成 HTTPS，只要用“https”协议即可。
如果要架设 FastCGI、uwsgi、SCGI、memcached 或者 gRPC 的负载均衡, 可以分别用 fastcgi_pass, uwsgi_pass, scgi_pass, memcached_pass, and grpc_pass 指令。

# 最少连接负载均衡
另一种负载均衡机制是最少连接。对于某些需要花比较长时间才能完成的请求，最少连接机制可以让程序实例的负载更加均衡。
使用最少连接这种负载均衡机制时，nginx 会尝试不让服务器负载过高，当有新请求时，会把请求分发给负载相对低的服务器。
在配置文件服务器组中，使用 least_conn 指令可以开启最少连接负载均衡机制：
```
upstream myapp1 {
    least_conn;
    server srv1.example.com;
    server srv2.example.com;
    server srv3.example.com;
}
```

# 会话保持
注意，使用轮询调度和最少连接负载均衡机制的时候，每个客户端的请求都有可能被分发到不同的服务器，无法保证同一个客户端的请求会被分发到同一台服务器。
如果需要把一个客户端绑定到一台特定的服务器，也就是说，让客户端的会话保持在一台固定的服务器，可以用 IP 地址哈希这种负载均衡机制。
使用 IP 地址哈希这种方式时，对于客户端的请求，客户端 IP 地址的哈希值是用来选择服务器组中服务器的关键。这种方法保证了同样客户端的请求，会被分发给同一台服务器，除非服务器不可用。
配置 IP 地址哈希负载均衡机制，只要在服务器组中加上 ip_hash 指令即可：
```
upstream myapp1 {
    ip_hash;
    server srv1.example.com;
    server srv2.example.com;
    server srv3.example.com;
}
```

# 权重负载均衡
还可以用服务器权重来支配 nginx 负载均衡的算法。
在上面的例子中，没有配置服务器的权重，意味着在一种负载均衡机制中，所有的服务器会被平等地对待。
特别是轮询调度机制，这也意味着服务器上请求的分配或多或少是相等的 —— 只要有足够的请求，并且以统一的方式处理，并且完成得足够快。
当给一台服务器配了 weight 参数，weight 会被当成负载均衡决策的一部分来处理。
```
upstream myapp1 {
    server srv1.example.com weight=3;
    server srv2.example.com;
    server srv3.example.com;
}
```
按上面的配置，每 5 个新请求会按以下方式分发给程序实例：3 个请求分发给 srv1，1 个请求分发给 srv2，另一个请求分发给 srv3。
在最近的 nginx 版本中，也可以在最少连接和 IP 地址哈希机制中类似地使用权重。

# 健康检查
nginx 的反向代理实现包含了带内（或被动的）服务器健康检查。如果某台服务器响应错误，nginx 会把这台服务器标记为失败，并且对接下来的请求不会分发给这台服务器。
max_fails 指令设置的是，在 fail_timeout 时间内，尝试和服务器沟通连续失败的次数。max_fails 的默认值是 1。当设为 0 时，表示不对这台服务器做健康检查。fail_timeout 参数同样定义了服务器多久会被标记为故障。在服务器故障之后，nginx 会间隔 fail_timeout 用客户端的请求去探测一下服务器。如果服务器探测成功，会被标记为可使用。
