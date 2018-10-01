---
title: 深入 Laravel 之请求的生命周期
date: 2018-05-12 11:13:23
tags: Laravel
---
> 请求的生命周期，即 Laravel 处理请求的整个流程。理解这个流程，有助于我们了解 Laravel 的整体架构。

# 主流程
## 流程图
![](https://i.loli.net/2018/05/13/5af7f2574aafe.png)


## index.php 分析

```php
<?php

// 加载 composer 的 autoloader ，用于处理类的加载
require __DIR__.'/../vendor/autoload.php';

// app.php 里面主要做了两件事：
// 1. 创建 IoC 容器
// 2. 绑定 http 请求处理核心程序、控制台命令处理核心程序和异常处理程序
$app = require_once __DIR__.'/../bootstrap/app.php';

// 从 IoC 容器中取出在 app.php 绑定的 http 请求处理程序
$kernel = $app->make(Illuminate\Contracts\Http\Kernel::class);

// Illuminate\Http\Request::capture() 主要是创建一个封装了 php 的 $_GET、$_POST、$_COOKIE、$_FILES、$_SERVER 全局变量的 Request 对象
//
// $kernel->handle() 主要做了以下事情：
// 1. 启动 Laravel ，包括：
//     1）加载 .env 的环境变量
//     2）加载 config 目录的配置
//     3）设置错误和异常的处理程序
//     4）注册 Facade
//     5）注册和启动 service provider
// 2. 通过全局中间件过滤和处理 Request 对象
// 3. 把 Request 对象分发给 Router 处理并返回 Response
$response = $kernel->handle(
    $request = Illuminate\Http\Request::capture()
);

// 把 response 返回给请求者
$response->send();

// 处理收尾工作，例如保存 session
$kernel->terminate($request, $response);

```

# 晦涩代码分析
```php
    protected function sendRequestThroughRouter($request)
    {
        ...

        return (new Pipeline($this->app))
                    ->send($request)
                    ->through($this->app->shouldSkipMiddleware() ? [] : $this->middleware)
                    ->then($this->dispatchToRouter());
    }
```
> 这里用到了管道设计模式。想了解管道设计模式，可以戳这个[链接](http://laravelacademy.org/post/3088.html)。
> 这里可以根据语义理解为，把 $request 发出去，通过 middleware 的处理后，分发给 router 。

```php
public function then(Closure $destination)
{
    $pipeline = array_reduce(
        array_reverse($this->pipes), $this->carry(), $this->prepareDestination($destination)
    );

    return $pipeline($this->passable);
}
```

> send() 和 through() 都是简单地把变量存下来，最后统一在 then() 处理，所以我们重点看一下 then() 的实现。

> array_reverse($this->pipes) 只是把 $this->pipes 的顺序逆转一下，比较难理解的是 $this->carry() 。
> 先看一下 array_reduce 的说明

```php
mixed array_reduce ( array $array , callable $callback [, mixed $initial = NULL ] )
```

> array_reduce() 将回调函数 callback 迭代地作用到 array 数组中的每一个单元中，从而将数组简化为单一的值。最好先看看[例子](http://php.net/manual/zh/function.array-reduce.php)。

> carry() 的实现如下：

```php
/**
 * Get a Closure that represents a slice of the application onion.
 *
 * @return \Closure
 */
protected function carry()
{
    return function ($stack, $pipe) {
        return function ($passable) use ($stack, $pipe) {
            if (is_callable($pipe)) {
                return $pipe($passable, $stack);
            } elseif (! is_object($pipe)) {
                list($name, $parameters) = $this->parsePipeString($pipe);

                $pipe = $this->getContainer()->make($name);

                $parameters = array_merge([$passable, $stack], $parameters);
            } else {
                $parameters = [$passable, $stack];
            }

            return method_exists($pipe, $this->method)
                            ? $pipe->{$this->method}(...$parameters)
                            : $pipe(...$parameters);
        };
    };
}
```

> 里面有两个闭包，第一个

```php
return function ($stack, $pipe) {
    ...
}
```
> 这个闭包是给 array_reduce 调用的，会把 $this->pipes ，也就是中间件，迭代地传进来
> 
> 第二个闭包，

```php
return function ($passable) use ($stack, $pipe) {
    ...
    return method_exists($pipe, $this->method)
                            ? $pipe->{$this->method}(...$parameters)
                            : $pipe(...$parameters);
}
```

> 主要是返回调用中间件 handle 方法的闭包

```php
$pipeline = array_reduce(
        array_reverse($this->pipes), $this->carry(), $this->prepareDestination($destination)
    );
```

** 执行完上面这句代码的时候，中间件还没有被执行，此时只是生成了一个使用了装饰者模式，把中间件一层一层，像洋葱一样包裹起来的闭包。 **

```php
return $pipeline($this->passable);
```

** 在执行这一句的时候，才会一层一层地真正执行中间件的代码。 **






