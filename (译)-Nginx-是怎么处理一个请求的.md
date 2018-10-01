---
title: (译) Nginx 是怎么处理一个请求的
date: 2018-07-15 22:01:45
tags: nginx
---

原文：[How nginx processes a request](http://nginx.org/en/docs/http/request_processing.html)

# 基于服务器名的虚拟服务器

首先，nginx 会先判断应该由哪个服务器来处理请求。我们来看一个简单的配置文件，这个配置文件配置了三个监听 80 端口的虚拟服务器：

```
server {
    listen      80;
    server_name example.org www.example.org;
    ...
}

server {
    listen      80;
    server_name example.net www.example.net;
    ...
}

server {
    listen      80;
    server_name example.com www.example.com;
    ...
}
```

在这个配置中，nginx 只会检测请求头的 “Host” 字段，来决定请求应该被路由到哪个服务器。如果没有匹配的域名，或者请求头没带 “Host” 字段，nginx 会把请求路由到监听这个端口的默认服务器。在上面的配置中，默认服务器是第一个，这是 nginx 的默认行为。也可以用 default_server 这个监听指令来显式地指定默认服务器。

```
server {
    listen      80 default_server;
    server_name example.net www.example.net;
    ...
}
```

> 0.8.21 之后的版本可以用 default_server 这个参数，而之前的版本应该用 default 

**需要注意一下，默认服务器是监听端口的一个属性，而不是服务器名的属性。**

# 如何不处理没有定义服务器名的请求

如果一个请求的请求头没有 “Host” 自动，那么这个请求不该被处理，一个丢弃这些请求的服务器配置可以这样定义：
```
server {
    listen      80;
    server_name "";
    return      444;
}
```
以上配置，服务器名设为一个空字符串，这样会匹配那些没设 “Host” 字段的请求，然后返回一个特殊的 nginx 的非标准返回码 444 并且关闭连接。
> 从 0.8.48 版本开始，这是服务器名的默认配置，所以 server_name "" 可以忽略掉。而在之前的版本，服务器的主机名会被用作默认服务器名。

# 基于服务器名和基于 IP 混合配置的虚拟服务器

我们来看一个复杂的配置，这个配置配置了几个监听不同地址的虚拟服务器：
```
server {
    listen      192.168.1.1:80;
    server_name example.org www.example.org;
    ...
}

server {
    listen      192.168.1.1:80;
    server_name example.net www.example.net;
    ...
}

server {
    listen      192.168.1.2:80;
    server_name example.com www.example.com;
    ...
}
```
在这个配置中，nginx 会先根据在 server 区块中配置的要监听的 IP 地址和端口来检测请求。然后在匹配的 IP 地址和端口的配置中，再检测请求的 “Host” 字段。如果服务器名没被找到，请求就会被默认服务器处理。举个例子，一个从 192.168.1.1:80 收到的，www.example.com 请求，会被 192.168.1.1:80 的默认服务器处理，也即给第一个服务器处理，因为这个端口没定义处理 www.example.com 的虚拟服务器。
如之前所说的，默认服务器是监听端口的一个属性，不同的端口可以定义不同的默认服务器：
```
server {
    listen      192.168.1.1:80;
    server_name example.org www.example.org;
    ...
}

server {
    listen      192.168.1.1:80 default_server;
    server_name example.net www.example.net;
    ...
}

server {
    listen      192.168.1.2:80 default_server;
    server_name example.com www.example.com;
    ...
}
```

# 一个简单的 PHP 网站的配置

我们来看一下，在一个经典的，简单的 PHP 网站中，nginx 处理请求的时候是怎么选择 location 的。
```
server {
    listen      80;
    server_name example.org www.example.org;
    root        /data/www;

    location / {
        index   index.html index.php;
    }

    location ~* \.(gif|jpg|png)$ {
        expires 30d;
    }

    location ~ \.php$ {
        fastcgi_pass  localhost:9000;
        fastcgi_param SCRIPT_FILENAME
                      $document_root$fastcgi_script_name;
        include       fastcgi_params;
    }
}
```

nginx 会先搜索特定字符串所给出的前缀位置，而不是考虑所列出的顺序。在上面的配置中，唯一的前缀位置是“/”，它会匹配所有的请求，所以最后才会用到。nginx 会按顺序检查在配置文件中列出的正则表达式，并使用第一个匹配的位置。如果没找到匹配的正则表达式，nginx 会用最特别的“/”位置。
需要注意的是，所有的位置只会检查请求的 URI 部分，不会检查参数。因为 query 参数可以按几种不同的方式给出，如：
```
/index.php?user=john&page=1
/index.php?page=1&user=john
```
另外，请求的 query 字符串可能出现随便什么东西：
```
/index.php?page=1&something+else&user=john
```
我们来看一下根据上面的配置，一个请求会被怎么处理：
- “logo.gif” 请求会先匹配前缀位置“/”，然后再匹配正则表达式“\.(gif|jpg|png)$”。因此这个请求会被后面的位置处理。因为用了“root /data/www”指令，所以请求会被映射到 /data/www/logo.gif 文件，然后这个文件就会被发给客户端。
- “/index.php” 也会先匹配前缀位置“/”，然后再匹配正则表达式“\.(php)$”。因此这个请求会被后面的 location 处理，请求会被传给监听 localhost:9000 的 FastCGI 服务器。fastcgi_param 指令设置了 FastCGI 参数 SCRIPT_FILENAME 为 “/data/www/index.php”，FastCGI 服务器执行这个文件。变量 $document_root 等于 root 指令定义的值，变量 $fastcgi_script_name 等于请求的 URI，即 “/index.php”。
- “/about.html”请求只匹配前缀位置“/”，因此会被这个 location 处理。因为用了“root /data/www”指令，所以请求会被映射到 /data/www/about.html 文件，然后这个文件就会被发给客户端。
- 处理“/”请求会复杂一点。请求只匹配前缀位置“/”，因此会被这个 location 处理。然后 index 指令会根据“root /data/www”指令设定的参数检查 index 文件的存在。如果 /data/www/index.html 文件不存在，而 /data/www/index.php 文件存在，index 指令会做一个内部的重定向到“index.php”，然后 nginx 会再次搜索 locations 。如我们之前看到的，被重定向的请求最终会被 FastCGI 服务器处理。






