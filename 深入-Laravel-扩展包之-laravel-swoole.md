---
title: 深入 Laravel 扩展包之 laravel-swoole
date: 2018-10-02 01:36:43
tags:
    - laravel
    - swoole
---

# 前言

Swoole 让 Laravel 的性能提升了很多，但 Laravel 并非按 Swoole 的架构去设计的，将两者结合在一起使用需要做一些处理。这篇文章让我们一起来看看 laravel-swoole 这个包是怎么把 Laravel 和 Swoole 结合起来的。

# Swoole 架构

## Swoole 架构

Swoole 是 laravel-swoole 这个包的基础，理解 Swoole 的架构是深入这个包的前提条件。

![](swoole-arch.png)

上图是 swoole 的架构图。大体上，我们需要了解以下几点：

- Master Process: 原来执行 PHP 脚本代码的进程，它会创建一个主 Reactor 和一个 Manager，它是整个应用程序的根进程

- Main Reactor: Reactor 在 Swoole 中是一个多线程和完全异步的，实现上使用了 Linux 的 epoll 或者 OSX 的 kqueue。Reactor 主要负责接收请求和分发给 Manager 进程。简单来说，它的功能相当于 nginx。

- Manager: Manager 进程会创建多个 Worker 进程。当有 Worker 结束的时候，它会根据配置的 Worker 数量，自动创建一个新的 Worker 进程。

- Worker: 所有请求的逻辑都会在 Worker 处理。

- Task Worker: 功能和 Worker 进程一样，但只处理 Worker 分发过来的任务，Workers 可以把任务异步地分发到任务队列。Task Workers 负责消费队列中的任务。

Laravel 工作在 Worker 进程。当 Worker 启动的时候，每个 Laravel 程序会被加载和初始化，且只会被加载和初始化一次。就是说 Laravel 会常驻内存，不需要每次处理一个请求就加载整个 Laravel。

这是我们加速 Laravel 的关键点，但同时也会带来一些奇奇怪怪的问题。容器是 Laravel 的核心，用于绑定、单例、解析程序、依赖注入等等。

一般来说，我们不需要关注全局属性、静态类、单例的使用，但现在要了。

因为 Laravel 会常驻内存，并且只会在程序启动的时候初始化一次，所以对 Laravel 的所有修改都会被保持，除非你重置它。

例如，auth 在 Laravel 中会被注册为单例。以下是 SessionGaurd 的部分代码：
```php
if (! is_null($this->user)) {
    return $this->user;
}
```
一段获取已经解析的用户对象的代码，看起来非常平常。但是在我们的场景下，这是一个致命的问题。在第一次解析了用户之后，这段代码会导致你获取了错误的用户对象。

有没有解决办法呢? 当然有，laravel-swoole 这个包提供了一个沙盒来防止这些 BUG。

## 沙盒容器

每个请求都会创建一个独立的沙盒来处理。

所有的容器会在一个沙盒容器中被处理，所有的变化不会影响 Laravel 的初始化状态。容器绑定也会在每个请求中被解析，这意味着单例的实例只会在当前的请求存在，不会干扰其他请求。

以下实例不需要重复解析，所以会被预先解析：
```
'view', 'files', 'session', 'session.store', 'routes',
'db', 'db.factory', 'cache', 'cache.store', 'config', 'cookies',
'encrypter', 'hash', 'router', 'translator', 'url', 'log'
```

> 在 v2.5.0 之后，你可以在 swoole_http.php 的 pre_resolved 配置中定制预解析列表。

还有一个 instances 配置，可以让你配置每个请求都还原。
```
'instances' => [
    'instanceA', 'instanceB'
],
```

> 这样做依然无法全局变量和静态变量的修改，如果你不清楚那些全局变量和静态变量是干嘛的，最后避免使用它们。

# 数据共享
## 数据共享

如上节所述，Laravel 的程序运行在不同的 Workder 进程中，变量在不同进程之间是无法共享的。

![](data-sharing.png)

每个 Worker 进程有自己的变量和内存分配，Laravel 常驻内存并不意味着我们可以在不同进程直接共享数据。

但我们可以有很多在进程间共享数据的办法：
- Databases 如 MySQL and Redis
- APCu - APC User Cache
- Swoole Table

## Swoole Table

在 swoole_http.php 配置中，你可以自定义 Swoole Table：

```php
use Swoole\Table;

'tables' => [
    // define your table name here
    'table_name' => [
        // table rows number
        'size' => 1024,
        // column name, column type and column type size are optional for int and float type
        'columns' => [
            ['name' => 'column_name1', 'type' => Table::TYPE_INT],
            ['name' => 'column_name2', 'type' => Table::TYPE_STRING, 'size' => 1024],
        ]
    ],
]
```

Swoole Table 有三种类型：
1. TYPE_INT: 1，2，4，8
2. TYPE_FLOAT: 8
3. TYPE_STRING: the nth power of 2

### Swoole Table 用法

```php
<?php

use SwooleTW\Http\Table\Facades\Table;

class Foo
{
    // get a table by its name
    $table = Table::get('table_name');

    // update a row of the table by key
    $table->set('key', 'value');

    // get a row of the table by key
    $table->get('key');

    // delete a row of the table by key
    $table->del('key');

    // check if a row is existed by key
    $table->exist('key');

    // count the rows in the table
    $table->count();
}
```


# 代码分析

## 源码结构

![](src-arch.png)

各目录的职责如下：
- Commands：实现 swoole:http 命令
- Controllers：提供给客户端请求 Websocket 的 API
- Server：http server 相关
- Table：Swoole Table 相关
- Websocket：Websocket 相关

其中最主要的代码是 LaravelServiceProvider.php、Server/Manager.php 和 Server/Sandbox.php。

## ServiceProvider

```php
<?php

namespace SwooleTW\Http;

abstract class HttpServiceProvider extends ServiceProvider
{
    ...

    /**
     * Register the service provider.
     *
     * @return void
     */
    public function register()
    {
        $this->mergeConfigs();
        $this->registerManager();
        $this->registerCommands();
    }

    ...
}
```

```php
<?php

namespace SwooleTW\Http;

use SwooleTW\Http\Server\Manager;

class LaravelServiceProvider extends HttpServiceProvider
{
    /**
     * Register manager.
     *
     * @return void
     */
    protected function registerManager()
    {
        $this->app->singleton('swoole.http', function ($app) {
            return new Manager($app, 'laravel');
        });
    }

    ...
}
```

可以看到，在 service provider 主要做了 3 件事：
- 整合配置
- 注册 http server manager
- 注册 artisan 命令


## Http Server Manager

```php
<?php

namespace SwooleTW\Http\Server;

...

class Manager
{
    ...

    /**
     * HTTP server manager constructor.
     *
     * @param \Illuminate\Contracts\Container\Container $container
     * @param string $framework
     * @param string $basePath
     */
    public function __construct(Container $container, $framework, $basePath = null)
    {
        $this->container = $container;
        $this->framework = $framework;
        $this->basePath = $basePath;

        $this->initialize();
    }

    /**
     * Run swoole server.
     */
    public function run()
    {
        $this->server->start();
    }

    /**
     * Stop swoole server.
     */
    public function stop()
    {
        $this->server->shutdown();
    }

    /**
     * "onStart" listener.
     */
    public function onStart()
    {
        $this->setProcessName('master process');
        $this->createPidFile();

        $this->container['events']->fire('swoole.start', func_get_args());
    }

    /**
     * "onWorkerStart" listener.
     */
    public function onWorkerStart(HttpServer $server)
    {
        $this->clearCache();
        $this->setProcessName('worker process');

        $this->container['events']->fire('swoole.workerStart', func_get_args());

        // don't init laravel app in task workers
        if ($server->taskworker) {
            return;
        }

        // clear events instance in case of repeated listeners in worker process
        Facade::clearResolvedInstance('events');

        // initialize laravel app
        $this->createApplication();
        $this->setLaravelApp();

        // bind after setting laravel app
        $this->bindToLaravelApp();

        // set application to sandbox environment
        $this->sandbox = Sandbox::make($this->getApplication());

        // load websocket handlers after binding websocket to laravel app
        if ($this->isWebsocket) {
            $this->setWebsocketHandler();
            $this->loadWebsocketRoutes();
        }
    }

    /**
     * "onRequest" listener.
     *
     * @param \Swoole\Http\Request $swooleRequest
     * @param \Swoole\Http\Response $swooleResponse
     */
    public function onRequest($swooleRequest, $swooleResponse)
    {
        $this->app['events']->fire('swoole.request');

        $this->resetOnRequest();

        $handleStatic = $this->container['config']->get('swoole_http.handle_static_files', true);

        try {
            // transform swoole request to illuminate request
            $illuminateRequest = Request::make($swooleRequest)->toIlluminate();

            // handle static file request first
            if ($handleStatic && $this->handleStaticRequest($illuminateRequest, $swooleResponse)) {
                return;
            }

            // set current request to sandbox
            $this->sandbox->setRequest($illuminateRequest);

            // enable sandbox
            $this->sandbox->enable();
            $application = $this->sandbox->getApplication();

            // handle request via laravel/lumen's dispatcher
            $illuminateResponse = $application->run($illuminateRequest);
            $response = Response::make($illuminateResponse, $swooleResponse);
            $response->send();
        } catch (Exception $e) {
            try {
                $exceptionResponse = $this->app[ExceptionHandler::class]->render($illuminateRequest, $e);
                $response = Response::make($exceptionResponse, $swooleResponse);
                $response->send();
            } catch (Exception $e) {
                $this->logServerError($e);
            }
        } finally {
            // disable and recycle sandbox resource
            $this->sandbox->disable();
        }
    }

    /**
     * Set onTask listener.
     */
    public function onTask(HttpServer $server, $taskId, $srcWorkerId, $data)
    {
        $this->container['events']->fire('swoole.task', func_get_args());

        try {
            // push websocket message
            if ($this->isWebsocket
                && array_key_exists('action', $data)
                && $data['action'] === Websocket::PUSH_ACTION) {
                $this->pushMessage($server, $data['data'] ?? []);
            }
        } catch (Exception $e) {
            $this->logServerError($e);
        }
    }

    /**
     * Set onShutdown listener.
     */
    public function onShutdown()
    {
        $this->removePidFile();
    }

    /**
     * Initialize.
     */
    protected function initialize()
    {
        $this->setProcessName('manager process');

        $this->createTables();
        $this->prepareWebsocket();
        $this->createSwooleServer();
        $this->configureSwooleServer();
        $this->setSwooleServerListeners();
    }

    ...
}
```

可以看到，Http Server Manager 的职责是对整个 Http Server 进行管理，包括初始化、启动 Server、停止 Server、监听事件等。

### 初始化主要做了以下工作：
- 创建 Swoole Table，用于进程间共享数据
- 初始化 Websocket
- 创建 Swoole Http Server
- 配置 Swoole Http Server
- 配置监听器

### run

启动 http server

### stop

关闭 http server

### onStart

http server 启动的时候回调该函数。在该函数中，主要把 http server 进程的 PID 记录到文件，发送 'swoole.start' 事件

### onWorkerStart

在 Worker 进程启动的时候回调该函数。在该函数中，主要做了以下事情：
- 清除 OPCode 缓存
- 发送 swoole.workerStart 事件
- 清除 Facade 已解析过的实例
- 创建 \SwooleTW\Http\Server\Application
- 绑定 swoole.server 和 swoole.table
- 创建沙盒

### onRequest

在收到一个完整的Http请求后，会回调此函数。在该函数中，主要做了以下事情：
- 发送 swoole.request 事件
- 处理静态文件的请求
- 开启沙盒
- 用 laravel/lumen 的分发器处理请求
- 返回结果给客户端

### onTask

在 task worker 进程内被调用。在该函数中，主要做了以下事情：
- 发送 swoole.task 事件
- 处理 Websocke 消息

### onShutdown

当 http server 正常结束时调用该函数。该函数主要删除记录 http server 进程 PID 的文件。


## Sandbox

```php
<?php

namespace SwooleTW\Http\Server;

...

class Sandbox
{
    ...

    /**
     * Sandbox constructor.
     */
    public function __construct(Application $application)
    {
        $this->setApplication($application);
        $this->setInitialConfig();
        $this->setInitialProviders();
    }

    ...

    /**
     * Get an application snapshot
     *
     * @return \SwooleTW\Http\Server\Application
     */
    public function getApplication()
    {
        if ($this->snapshot instanceOf Application) {
            return $this->snapshot;
        } elseif (! $this->enabled) {
            throw new SandboxException('Sandbox is not enabled yet.');
        }

        return $this->snapshot = clone $this->application;
    }

    /**
     * Reset Laravel/Lumen Application.
     */
    protected function resetLaravelApp($application)
    {
        $this->resetConfigInstance($application);
        $this->resetSession($application);
        $this->resetCookie($application);
        $this->clearInstances($application);
        $this->bindRequest($application);
        $this->rebindRouterContainer($application);
        $this->rebindViewContainer($application);
        $this->resetProviders($application);
    }

    ...

    /**
     * Set laravel snapshot to container and facade.
     */
    public function enable()
    {
        $this->enabled = true;
        $this->setInstance($app = $this->getLaravelApp());
        $this->resetLaravelApp($app);
    }

    ...
}
```

sandbox 会在 Worker 进程启动的时候创建，每次接收到请求的时候 enable，所以沙盒的代码中，主要部分是构造函数和 enable 。

### 构造函数
主要做的事情：
- 设置 SwooleTW\Http\Serve\Application 实例
- 复制一份 laravel 的 config 
- 初始化自定义的 service providers

### enable
主要做的事情：
- 设置一个 laravel 容器快照到沙盒中
- reset laravel 绑定的如 config、session、cookie 等实例

沙盒在每次 enable 的时候，都会设置一个 laravel 容器的快照到沙盒中，每次接收到新的请求，Swoole Server Manager 中 onRequest 的回调都会通过沙盒来执行程序，实现隔离所有请求的效果。


# 总结

这篇文章中，我们了解以下几点：
- Swoole 架构
- 进程间共享数据的 Swoole Table
- 源码结构
- ServiceProvider、Swoole Http Manager 和 Sandbox 源码分析

# 参考

https://github.com/swooletw/laravel-swoole/wiki
