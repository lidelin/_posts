---
title: 深入 Laravel 之服务容器
date: 2018-05-24 23:30:35
tags: Laravel
---

# 前言
上篇文章[《设计模式之依赖注入和依赖反转》](https://lidelin.github.io/2018/05/20/%E8%AE%BE%E8%AE%A1%E6%A8%A1%E5%BC%8F%E4%B9%8B%E4%BE%9D%E8%B5%96%E6%B3%A8%E5%85%A5%E5%92%8C%E4%BE%9D%E8%B5%96%E5%8F%8D%E8%BD%AC/)复习了依赖注入和依赖反转，这篇文章我们来深入探索一下服务容器。
服务容器是用来管理类的依赖和实现依赖注入的一个很有用的工具。这里要注意一下，容器仅仅是个工具，可以让我们很方便地实现依赖注入而已，不是必需的。
深入理解 Laravel 的服务容器，对于构建大型应用程序，或者给 Laravel 贡献代码，是必不可少的。

> PS
> - 下面写到的 $abstract 可以是一个 interface 名，也可以是一个 string 标识，也可以是一个类，所以大部分情况我都是直接写 $abstract  ，具体要看绑定的时候传的值
> - $concrete 可以是一个类，可以是一个类实例，也可以是一个闭包，所以大部分情况都直接写 $concrete ，具体要看绑定的时候传的值

# 用法
## 绑定
### 基础绑定
#### 简单绑定
```php
$this->app->bind($abstract, $concrete);
```
- $abstract 是一个字符串；$concrete 可以是一个类，Closure 或者 null。如果 $concrete 为 null 时，会拿 $abstract 来实例化
- 如果 $abstract 之前已经绑定过其他 $concrete ，并且已经实例化过，laravel 马上实例化新绑定的 $concrete ，并且调用 reboundCallbacks 

#### 绑定一个单例
```php
$this->app->singleton($abstract, $concrete);
```

#### 绑定类实例
```php
$this->app->instance($abstract, $instance);
```

#### 绑定初始值
有时候一个不仅要接受注入另一个类，还需要注入一些初始值（比如一个整数）。这个可以用上下文绑定来实现。
```php
$this->app->when('App\Http\Controllers\UserController')
          ->needs('$variableName')
          ->give($value);
```

### 给接口绑定实现
```php
$this->app->bind(
    'App\Contracts\EventPusher',
    'App\Services\RedisEventPusher'
);
```
这样绑定之后，当一个类需要注入 interface EventPusher ，容器就会把 RedisEventPusher 注入进去。

### 上下文绑定
两个不同的类用了同一个接口，注入的时候需要又需要注入不同的实现，可以用上下文绑定。
```php
use Illuminate\Support\Facades\Storage;
use App\Http\Controllers\PhotoController;
use App\Http\Controllers\VideoController;
use Illuminate\Contracts\Filesystem\Filesystem;

$this->app->when(PhotoController::class)
          ->needs(Filesystem::class)
          ->give(function () {
              return Storage::disk('local');
          });

$this->app->when(VideoController::class)
          ->needs(Filesystem::class)
          ->give(function () {
              return Storage::disk('s3');
          });
```

### 标记
有时候，我们会需要一组类别相同的绑定，比如：
```php
$this->app->bind('SpeedReport', function () {});

$this->app->bind('MemoryReport', function () {});

$this->app->tag(['SpeedReport', 'MemoryReport'], 'reports');
```
这样调用之后，我们就可以用 tagged 方法来获取所有 reports 的实例了
```php
$this->app->bind('ReportAggregator', function ($app) {
    return new ReportAggregator($app->tagged('reports'));
});
```

### 绑定扩展
```php
$this->app->extend(Service::class, function($service) {
    return new DecoratedService($service);
});
```

## 解析
### make 方法
```php
$api = $this->app->make('HelpSpot\API');

$api = resolve('HelpSpot\API');
```

当类的依赖不能从容器中解析出来，可以用 makeWith 方法：
```php
$api = $this->app->makeWith('HelpSpot\API', ['id' => 1]);
```

### 自动注入
可以直接在 controllers, event listeners, queue jobs, middleware 等类的构造函数声明类的依赖，容器会自动解析并注入需要的依赖。

## 容器的事件
每当服务容器解析一个对象时触发一个事件。可以使用 resolving 方法监听这个事件：
```php
$this->app->resolving(function ($object, $app) {
    // Called when container resolves object of any type...
});

$this->app->resolving(HelpSpot\API::class, function ($api, $app) {
    // Called when container resolves objects of type "HelpSpot\API"...
});
```

## PSR-11
Laravel 的服务容器实现了 PSR-11 的接口，所以可以直接用 PSR-11 的容器接口来获取 laravel 的容器。
```php
use Psr\Container\ContainerInterface;

Route::get('/', function (ContainerInterface $container) {
    $service = $container->get('Service');
     //
});
```

# 源码解读
## 概览
因为在 bootstrap/app.php 中实例化的是 Illuminate\Foundation\Application ，所以我们从这个类看看 UML 图，如下：
![WechatIMG246.jpeg](https://i.loli.net/2018/05/27/5b0a6c9421486.jpeg)

## Psr\Container\ContainerInterface
```php
namespace Psr\Container;

interface ContainerInterface
{
    /**
     * 通过 $id 从容器找出对应的实例
     */
    public function get($id);

    /**
     * 如果容器中有 $id 对应的实例，则返回 true，否则返回 false
     */
    public function has($id);
}
```

## Illuminate\Contracts\Container\Container
```php
<?php

namespace Illuminate\Contracts\Container;

use Closure;
use Psr\Container\ContainerInterface;

interface Container extends ContainerInterface
{
    /**
     * 确定 $abstract 有没有被绑定过
     */
    public function bound($abstract);

    /**
     * 给 $abstract 起一个别名
     */
    public function alias($abstract, $alias);

    /**
     * 给一系列的绑定打个标记
     */
    public function tag($abstracts, $tags);

    /**
     * 解析某个标记的所有实例
     */
    public function tagged($tag);

    /**
     * 与容器注册绑定
     */
    public function bind($abstract, $concrete = null, $shared = false);

    /**
     * 如果 $abstract 没注册过绑定，则注册绑定
     */
    public function bindIf($abstract, $concrete = null, $shared = false);

    /**
     * 在容器中注册一个共享的绑定，即单例
     */
    public function singleton($abstract, $concrete = null);

    /**
     * 扩展容器中 $abstract 的实例
     */
    public function extend($abstract, Closure $closure);

    /**
     * 用一个类实例与容器注册一个共享的绑定
     */
    public function instance($abstract, $instance);

    /**
     * 定义一个上下文绑定
     */
    public function when($concrete);

    /**
     * 从容器获取一个闭包来解析 $abstract 的实例
     */
    public function factory($abstract);

    /**
     * 从容器解析 $abstract 的实例并返回
     */
    public function make($abstract, array $parameters = []);

    /**
     * 调用给出的闭包或者类方法，并且注入所需的依赖
     */
    public function call($callback, array $parameters = [], $defaultMethod = null);

    /**
     * 确定 $abstract 是否被解析过
     */
    public function resolved($abstract);

    /**
     * 注册容器解析对象时的回调
     */
    public function resolving($abstract, Closure $callback = null);

    /**
     * 注册调用完 resolving 注册的回调的回调
     * 有点绕，就是调用了上面 resolving 注册的回调之后，
     * 就会调用 afterResolving 注册的回调
     */
    public function afterResolving($abstract, Closure $callback = null);
}

```


## bind
```php
/**
 * Register a binding with the container.
 *
 * @param  string  $abstract
 * @param  \Closure|string|null  $concrete
 * @param  bool  $shared
 * @return void
 */
public function bind($abstract, $concrete = null, $shared = false)
{
    // 把旧的绑定 unset 掉
    $this->dropStaleInstances($abstract);

    if (is_null($concrete)) {
        $concrete = $abstract;
    }

    // 如果 $concrete 不是 Closure ，就是一个类，
    // 为了后续使用方便，这里会把类转出一个闭包
    if (! $concrete instanceof Closure) {
        $concrete = $this->getClosure($abstract, $concrete);
    }

    // 把绑定存到 $this->bindings
    $this->bindings[$abstract] = compact('concrete', 'shared');

    // 如果 $abstract 之前已经绑定过其他 $concrete ，
    // 并且已经实例化过，laravel 马上实例化新绑定的 $concrete ，
    // 并且调用 reboundCallbacks
    if ($this->resolved($abstract)) {
        $this->rebound($abstract);
    }
}
```

## singleton
```php
/**
 * Register a shared binding in the container.
 *
 * @param  string  $abstract
 * @param  \Closure|string|null  $concrete
 * @return void
 */
public function singleton($abstract, $concrete = null)
{
    $this->bind($abstract, $concrete, true);
}
```
singleton 很简单，仅仅把 bind 的第三个参数设为 true ，在实例化完之后，就会把对象存到 $this->instances 数组中，下次需要使用这个对象的时候，直接取出来。

## instance
```php
/**
 * Register an existing instance as shared in the container.
 *
 * @param  string  $abstract
 * @param  mixed   $instance
 * @return mixed
 */
public function instance($abstract, $instance)
{
    // 移除 $abstract 之前绑定过的别名
    $this->removeAbstractAlias($abstract);

    // 判断 $abstract 之前是否已经绑定过类实例了
    $isBound = $this->bound($abstract);

    unset($this->aliases[$abstract]);

    // 保存新的实例
    $this->instances[$abstract] = $instance;

    // 如果之前绑定过实例，则触发重新绑定的处理
    // 比如调用 rebound callbacks
    if ($isBound) {
        $this->rebound($abstract);
    }

    return $instance;
}
```

## make
我们重点看一下 make()

```php
public function make($abstract, array $parameters = [])
{
    return $this->resolve($abstract, $parameters);
}

protected function resolve($abstract, $parameters = [])
{
    // 获取 $abstract 真正的类
    $abstract = $this->getAlias($abstract);

    // 确定是否需要上下文构建
    $needsContextualBuild = ! empty($parameters) || ! is_null(
        $this->getContextualConcrete($abstract)
    );

    // 如果绑定的是一个单例，并且已经实例化，
    // 而且不需要上下文构建，则直接返回类实例
    if (isset($this->instances[$abstract]) && ! $needsContextualBuild) {
        return $this->instances[$abstract];
    }

    $this->with[] = $parameters;

    // 获取之前绑定时准备好的闭包
    $concrete = $this->getConcrete($abstract);

    if ($this->isBuildable($concrete, $abstract)) {
        // 如果 $concrete 和 $abstract 是相同的类，
        // 或者 $concrete 是一个闭包，
        // 则构建实例
        $object = $this->build($concrete);
    } else {
        // 递归调用来解析出所有的实例
        $object = $this->make($concrete);
    }

    // 调用之前注册的扩展回调，decorate 上面创建的对象
    foreach ($this->getExtenders($abstract) as $extender) {
        $object = $extender($object, $this);
    }

    // 如果 $abstract 注册的绑定是单例，则把上面创建的对象保存到 $this->instances
    if ($this->isShared($abstract) && ! $needsContextualBuild) {
        $this->instances[$abstract] = $object;
    }

    // 触发绑定事件，调用之前注册的回调
    $this->fireResolvingCallbacks($abstract, $object);

    // 记录已经解析的 $abstract
    $this->resolved[$abstract] = true;

    array_pop($this->with);

    return $object;
}

public function build($concrete)
{
    // 如果 $concrete 是闭包，则直接调用并返回
    if ($concrete instanceof Closure) {
        return $concrete($this, $this->getLastParameterOverride());
    }

    // 如果 $concrete 是类，则用反射去解析
    
    $reflector = new ReflectionClass($concrete);

    // 如果 $concrete 不能实例化，
    // 则抛出 \Illuminate\Contracts\Container\BindingResolutionException 
    if (! $reflector->isInstantiable()) {
        return $this->notInstantiable($concrete);
    }

    // 把 $concrete 类放到构建栈，因为要构建 $concrete 构造函数的依赖
    $this->buildStack[] = $concrete;

    $constructor = $reflector->getConstructor();

    // 如果没有构造函数，说明没有依赖，则直接 new $concrete 并返回
    if (is_null($constructor)) {
        array_pop($this->buildStack);

        return new $concrete;
    }

    $dependencies = $constructor->getParameters();

    // 解析构造函数的所有依赖，
    // 如果依赖是一个类，则会调用 public function make($abstract, array $parameters = []) 去解析出实例，
    // 直到所有的依赖都解析出来
    $instances = $this->resolveDependencies(
        $dependencies
    );

    array_pop($this->buildStack);

    return $reflector->newInstanceArgs($instances);
}
```




