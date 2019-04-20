---
title: 基于 Docker 从零搭建 LNMP 环境
date: 2018-08-11 16:29:51
tags: 
    - docker
    - lnmp
---

上周有位同事让我分享一下怎么用 docker，由于之前一直在用 [laradock](http://laradock.io/) ，还没试过基于 Docker 搭建 LNMP 环境，今天借着做作业的机会来实践总结一下。

# 什么是 Docker

参考[这篇文章](http://www.docker.org.cn/book/docker/what-is-docker-16.html)的定义：Docker是一个开源的引擎，可以轻松的为任何应用创建一个轻量级的、可移植的、自给自足的容器。
**简单粗暴来理解的话，可以把 Docker 理解成虚拟机，** 但实际上却有很大区别。

<!--more-->

Docker 和虚拟机的对比如下图：
[![WechatIMG347.jpeg](https://i.loli.net/2018/08/11/5b6eb221c261b.jpeg)](https://i.loli.net/2018/08/11/5b6eb221c261b.jpeg)

从上图可以看出，我们把 app 和 app 的依赖打包进容器，多个容器共用宿主机的资源，并且在运行的时候相互隔离。虚拟机也是运行在宿主机上，一台宿主机也可以运行多个虚拟机，但是每个虚拟机都要重新装一个操作系统。
相比之下，容器相当于一个运行在操作系统上的进程，所占用的资源比虚拟机要少很多。所以容器比虚拟机轻量很多且高效很多。

# Docker 基本操作

## 配置镜像加速器

使用阿里云的镜像加速器，配置文档可参考[《Docker 镜像加速器》](https://yq.aliyun.com/articles/29941)。

## hello docker
```bash
docker run ubuntu echo hello docker
```
执行结果如图：

[![docker-hello.jpeg](https://i.loli.net/2018/08/11/5b6ed93ae6817.jpeg)](https://i.loli.net/2018/08/11/5b6ed93ae6817.jpeg)

命令解释：
- docker: docker 命令
- run: 运行某个容器
- ubuntu: 指定要运行的镜像，Docker 首先从本地主机上查找镜像是否存在，如果不存在，Docker 就会从镜像仓库 Docker Hub 下载公共镜像。
- echo hello docker: 在启动的容器里面执行的命令

## Docker 常用命令
这里只列一些个人常用的命令，更详细的命令说明可以参考官网或者[这里](http://www.runoob.com/docker/docker-command-manual.html)。

- run: 创建一个新的容器并运行一个命令
    语法: docker run [OPTIONS] IMAGE [COMMAND] [ARG...]
    
    OPTIONS:
    -d: 后台运行容器，并返回容器ID；
    -i: 以交互模式运行容器，通常与 -t 同时使用；
    -p: 端口映射，格式为：主机(宿主)端口:容器端口
    -t: 为容器重新分配一个伪输入终端，通常与 -i 同时使用；
    -v: 绑定一个挂在分区

- start/stop/restart: 启动/停止/重新启动 一个容器
- rm: 删除一个或多少容器
- exec: 在运行的容器中执行命令
- ps: 列出容器
- commit: 从容器创建一个新的镜像
- cp: 用于容器与主机之间的数据拷贝
- images: 列出本地镜像
- rmi: 删除本地一个或多少镜像
- build: 用于使用 Dockerfile 创建镜像

# Dockerfile
Docker 可以从一个名为 Dockerfile 的文件读取一系列指令来自动构建镜像。Dockerfile 文件包含了所有构建镜像的命令。用户通过 docker build 命令可以自动地构建镜像。
Dockerfile 的语法可以参考[官方文档](https://docs.docker.com/engine/reference/builder/)。

# Docker Compose
Docker Compose 是一个用户定义和运行多个容器的 Docker 应用程序。在 Compose 中你可以使用 YAML 文件来配置你的应用服务。然后，只需要一个简单的命令，就可以创建并启动你配置的所有服务。
使用 Compose 基本会有如下三步流程：

1. 在 Dockfile 中定义你的应用环境，使其可以在任何地方复制。
2. 在 docker-compose.yml 中定义组成应用程序的服务，以便它们可以在隔离的环境中一起运行。
3. 最后，运行dcoker-compose up，Compose 将启动并运行整个应用程序。

详细的说明可以参考[官方文档](https://docs.docker.com/compose/compose-file/)

# 构建 LNMP

我们可以在 https://hub.docker.com/ 搜索到常用的镜像，如 php-fpm、nginx、mysql 等，在编写 Dockfile 的时候，可以参考每个镜像的说明。

## workspace
Dockerfile 如下：
```
# 基于 docker hub 的 php:7.2-fpm 的镜像
FROM php:7.2-fpm

# 维护者信息
LABEL maintainer="Wilson.Li <494747693@qq.com>"

# 安装所需的依赖
RUN apt-get update && apt-get install -y \
        libfreetype6-dev \
        libjpeg62-turbo-dev \
        libpng-dev \
    && docker-php-ext-install -j$(nproc) iconv \
    && docker-php-ext-configure gd --with-freetype-dir=/usr/include/ --with-jpeg-dir=/usr/include/ \
    && docker-php-ext-install -j$(nproc) gd \
    && docker-php-ext-install pdo \
    && docker-php-ext-install pdo_mysql

# 设置容器的工作目录
WORKDIR /var/www
```

## nginx
Dockerfile 如下：
```
# 基于 docker hub 的 nginx:alpine 镜像
FROM nginx:alpine

# 维护者信息
LABEL maintainer="Wilson.Li <494747693@qq.com>"

# 添加虚拟主机配置
ADD default.conf /etc/nginx/conf.d/default.conf

# 指定容器的工作目录
WORKDIR /var/www
```

## mysql
由于我们没有对 mysql 的镜像做任何修改，所以可以直接在 docker-compose.yml 中用 image 标签来指定。

## docker-compose.yml
```yaml
# 指定使用的 docker-compose 的版本
version: '3'

services:

  # 配置 workspace
  workspace:
    # 配置编译相关的信息
    build:
      # 指定读取 Dockfile 的目录
      context: ./workspace
    # 配置容器工作目录
    working_dir: /var/www
    # 配置挂在的目录，./code 是宿主机的路径，/var/www 是容器中的路径
    volumes:
      - ./code:/var/www
    # 配置所需的环节变量
    environment:
      - "DB_PORT=3306"
      - "DB_HOST=mysql"

  # 配置 nginx
  nginx:
    build:
      context: ./nginx
    working_dir: /var/www
    volumes:
      - ./code:/var/www
    # 配置端口映射信息
    ports:
      - 8180:80

  # 配置 mysql 数据库
  mysql:
    # 直接使用 mysql:5.7 的镜像
    image: mysql:5.7
    volumes:
      - ./data/mysql:/var/lib/mysql
    environment:
      - "MYSQL_DATABASE=homestead"
      - "MYSQL_USER=homestead"
      - "MYSQL_PASSWORD=secret"
      - "MYSQL_ROOT_PASSWORD=secret"
    ports:
        - "3306:3306"
```

## 目录结构
```
.
├── code
│   └── laravel
├── data
│   └── mysql
├── docker-compose.yml
├── nginx
│   ├── Dockerfile
│   └── default.conf
└── workspace
    └── Dockerfile
```

## 运行
在 docker-compose.yml 同级目录输入以下命令：
```
docker-compose up -d
```
-d 表示在后台运行

## 查看运行状态
```
docker-compose ps
```
运行状态如下图：
[![WechatIMG349.jpeg](https://i.loli.net/2018/08/13/5b706285cfd97.jpeg)](https://i.loli.net/2018/08/13/5b706285cfd97.jpeg)

## 进入容器命令
```
docker-compose exec workspace bash
```

## 页面访问

因为我们在 docker-compose.yml 配置的 nginx 端口映射为 8180:80，所以我们访问的网址为: http://localhost:8180 
我在 mac 上可以正常访问，访问结果就不截图了。

## 数据库访问

由于我们在 docker-compose.yml 配置 workspace 了环境变量
```
environment:
  - "DB_PORT=3306"
  - "DB_HOST=mysql"
```
所以，我们在 laravel 的 .env 中，把 DB_HOST 配置为 mysql 即可访问数据库。



