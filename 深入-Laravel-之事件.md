---
title: 深入 Laravel 之事件
date: 2018-06-30 11:38:07
tags: Laravel
---

Laravel 的事件系统提供了一个观察者模式的简单实现，让你可以监听和订阅程序中的各种事件。

<!--more-->

# 基本用法

## 定义事件
```bash
php artisan make:event OrderShipped
```
以上命令会在 app/Events 目录生成 OrderShipped

```php
<?php

namespace App\Events;

use App\Order;
use Illuminate\Queue\SerializesModels;

class OrderShipped
{
    use SerializesModels;

    public $order;

    /**
     * Create a new event instance.
     *
     * @param  \App\Order  $order
     * @return void
     */
    public function __construct(Order $order)
    {
        $this->order = $order;
    }
}
```

## 定义监听器
在 app/Listeners 目录添加 SendShipmentNotification.php
```php
<?php

namespace App\Listeners;

use App\Events\OrderShipped;

class SendShipmentNotification
{
    /**
     * Create the event listener.
     *
     * @return void
     */
    public function __construct()
    {
        //
    }

    /**
     * Handle the event.
     *
     * @param  OrderShipped  $event
     * @return void
     */
    public function handle(OrderShipped $event)
    {
        // Access the order using $event->order...
    }
}

```

当监听处理的事情比较耗时，而且可以异步处理的话，可以把监听器放到队列执行。
配置好队列后，把监听器放到队列执行，只需要 implement Illuminate\Contracts\Queue\ShouldQueue 即可。
```php
<?php

namespace App\Listeners;

use App\Events\OrderShipped;
use Illuminate\Contracts\Queue\ShouldQueue;

class SendShipmentNotification implements ShouldQueue
{
    //
}
```

## 注册事件和监听器

在 App\Providers\EventServiceProvider 注册事件和监听器
```php
<?php

namespace App\Providers;

class EventServiceProvider
{
    ...
    
    protected $listen = [
        'App\Events\OrderShipped' => [
            'App\Listeners\SendShipmentNotification',
        ],
    ];
    
    ...
}
```

## 分发事件
分发事件使用 event() helper 即可
```php
<?php
namespace App\Http\Controllers;

use App\Order;
use App\Events\OrderShipped;
use App\Http\Controllers\Controller;

class OrderController extends Controller
{
    /**
     * Ship the given order.
     *
     * @param  int  $orderId
     * @return Response
     */
    public function ship($orderId)
    {
        $order = Order::findOrFail($orderId);

        // Order shipment logic...

        event(new OrderShipped($order));
    }
}
```

# 代码分析

## 负责接入 Laravel 的 EventServiceProvider

负责介入 Laravel 的 EventServiceProvider，即 Illuminate\Events\EventServiceProvider。

```php
<?php

namespace Illuminate\Events;

use Illuminate\Support\ServiceProvider;
use Illuminate\Contracts\Queue\Factory as QueueFactoryContract;

class EventServiceProvider extends ServiceProvider
{
    /**
     * Register the service provider.
     *
     * @return void
     */
    public function register()
    {
        $this->app->singleton('events', function ($app) {
            return (new Dispatcher($app))->setQueueResolver(function () use ($app) {
                return $app->make(QueueFactoryContract::class);
            });
        });
    }
}
```
可以看到，这里做的事情很简单，只注册了一个 events 的绑定。
这个 service provider 会在 Illuminate\Foundation\Application@registerBaseServiceProviders 注册，所以 Laravel 容器初始化完之后，这个 service provider 就已经注册好了。

## 负责管理事件和监听器关系的 EventServiceProvider

负责管理事件和监听器关系的 EventServiceProvider，即 App\Providers\EventServiceProvider。
我们在 config/app.php 的 providers 可以看到，

```php
<?php

return [
    ...
    
    'providers' => [
        ...
        
        App\Providers\EventServiceProvider::class,
        
        ...
    ],
    
    ...
];
```

所以当 http kernel 跑起来后，事件和监听器关系便注册好了。

我们来看看 Illuminate\Foundation\Support\Providers\EventServiceProvider 的代码。
```php
<?php

namespace Illuminate\Foundation\Support\Providers;

...

class EventServiceProvider extends ServiceProvider
{
    ...

    public function boot()
    {
        foreach ($this->listens() as $event => $listeners) {
            foreach ($listeners as $listener) {
                Event::listen($event, $listener);
            }
        }

        foreach ($this->subscribe as $subscriber) {
            Event::subscribe($subscriber);
        }
    }

    ...
}

```

这里主要是调用 Illuminate\Events\Dispatcher 的 listen($events, $listener) 来注册事件和监听器，还有 subscribe($subscriber) 用来注册订阅者。

## 事件系统的核心 —— Dispatcher

### listen

我们先看一下 Dispatcher 的 listen($events, $listener)

```php
<?php

namespace Illuminate\Events;

...

class Dispatcher implements DispatcherContract
{
    ...
    
    public function listen($events, $listener)
    {
        foreach ((array) $events as $event) {
            if (Str::contains($event, '*')) {
                $this->setupWildcardListen($event, $listener);
            } else {
                $this->listeners[$event][] = $this->makeListener($listener);
            }
        }
    }
    
    ...
}
```

makeListener($listener, $wildcard = false) 会返回一个闭包，对于通配符 '*' 的事件，会把 $wildcard 设为 true。我们再看看和创建 listener 相关的实现。

```php
<?php

namespace Illuminate\Events;

...

class Dispatcher implements DispatcherContract
{
    ...
    
    public function makeListener($listener, $wildcard = false)
    {
        if (is_string($listener)) {
            return $this->createClassListener($listener, $wildcard);
        }

        return function ($event, $payload) use ($listener, $wildcard) {
            if ($wildcard) {
                return $listener($event, $payload);
            }

            return $listener(...array_values($payload));
        };
    }
    
    public function createClassListener($listener, $wildcard = false)
    {
        return function ($event, $payload) use ($listener, $wildcard) {
            if ($wildcard) {
                return call_user_func($this->createClassCallable($listener), $event, $payload);
            }

            return call_user_func_array(
                $this->createClassCallable($listener), $payload
            );
        };
    }
    
    protected function createClassCallable($listener)
    {
        list($class, $method) = $this->parseClassCallable($listener);
        
        if ($this->handlerShouldBeQueued($class)) {
            return $this->createQueuedHandlerCallable($class, $method);
        }

        return [$this->container->make($class), $method];
    }
    
    protected function handlerShouldBeQueued($class)
    {
        try {
            return (new ReflectionClass($class))->implementsInterface(
                ShouldQueue::class
            );
        } catch (Exception $e) {
            return false;
        }
    }
    
    protected function createQueuedHandlerCallable($class, $method)
    {
        return function () use ($class, $method) {
            $arguments = array_map(function ($a) {
                return is_object($a) ? clone $a : $a;
            }, func_get_args());

            if ($this->handlerWantsToBeQueued($class, $arguments)) {
                $this->queueHandler($class, $method, $arguments);
            }
        };
    }
    
    protected function queueHandler($class, $method, $arguments)
    {
        list($listener, $job) = $this->createListenerAndJob($class, $method, $arguments);

        $connection = $this->resolveQueue()->connection(
            $listener->connection ?? null
        );

        $queue = $listener->queue ?? null;

        isset($listener->delay)
                    ? $connection->laterOn($queue, $listener->delay, $job)
                    : $connection->pushOn($queue, $job);
    }
    
    ...
}
```

从上面可以整理出流程：把注册的 Listener 类转成一个闭包，通过反射
判断 listener 有没有实现 Illuminate\Contracts\Queue\ShouldQueue，如果有，就生成一个把 listener 放进队列执行的闭包。

### dispatch

event helper 实际上会调用 dispatch($event, $payload = [], $halt = false) 

```php
<?php

namespace Illuminate\Events;

...

class Dispatcher implements DispatcherContract
{
    ...
    
    public function dispatch($event, $payload = [], $halt = false)
    {
        list($event, $payload) = $this->parseEventAndPayload(
            $event, $payload
        );
        
        if ($this->shouldBroadcast($payload)) {
            $this->broadcastEvent($payload[0]);
        }

        $responses = [];
        
        foreach ($this->getListeners($event) as $listener) {
            $response = $listener($event, $payload);

            if ($halt && ! is_null($response)) {
                return $response;
            }

            if ($response === false) {
                break;
            }

            $responses[] = $response;
        }

        return $halt ? null : $responses;
    }
    
    ...
}
```

以上代码做了以下事情：
- 当传进来的是一个事件对象，会把事件的类名当成事件名，事件对象本身当做 payload
- 把事件的所有监听器取出来执行
- 如果 $halt 为 true，遇到一个 response 不为 null 时，会直接返回 response
- 如果其中一个监听器执行完之后，返回了 false，就会中断执行剩下的监听器









