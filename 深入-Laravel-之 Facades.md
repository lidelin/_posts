---
title: 深入 Laravel 之 Facades
date: 2018-06-07 21:32:58
tags: Laravel
---

Facade 一般中文翻译为 “门面”，是一种设计模式。

# Facade 优点
- 让 Api 变得简单清晰
    比如 \Illuminate\Cache\CacheManager::get，
    我们可以直接调用 \Cache::get 这样使用，不用记那一长串\Illuminate\Cache\CacheManager 。

- 方便测试
    laravel 的 Facade 帮我们实现了 shouldReceive() 、spy() 等测试方法，使我们写测试代码的时候非常方便。
    
    ```php
use Illuminate\Support\Facades\Cache;

public function testBasicExample()
{
    Cache::shouldReceive('get')
         ->with('key')
         ->andReturn('value');

    $this->visit('/cache')
         ->see('value');
}
    ```




# Facades 工作原理
在 Laravel 中，Facade 就是一个可以从容器中访问对象的一个类。当我们以静态函数方式去调用某个方法的时候，就会触发 __callStatic ，然后通过容器解析出对应的实例，再调用实例的方法。

# 代码分析

## UML 图

以 Cache 为例，我们看一下 UML

![](https://i.loli.net/2018/06/08/5b1a8f3c71713.jpeg)

从上图可以看出，
Illuminate\Support\Facades\Cache 仅仅继承了 Illuminate\Support\Facades\Facade ，实现非常简单

接下来我们看看 Illuminate\Support\Facades\Facade 的代码

## Illuminate\Support\Facades\Facade

```php
<?php

namespace Illuminate\Support\Facades;

use Mockery;
use RuntimeException;
use Mockery\MockInterface;

abstract class Facade
{
    // laravel 容器
    protected static $app;

    // 解析过的实例
    protected static $resolvedInstance;

    // 用于单元测试场景，忽略所有没被告知期望值的调用
    public static function spy()
    {
        if (! static::isMock()) {
            $class = static::getMockableClass();

            static::swap($class ? Mockery::spy($class) : Mockery::spy());
        }
    }

    // 用于单元测试场景，定义将被调用的方法，以及其返回值
    public static function shouldReceive()
    {
        $name = static::getFacadeAccessor();

        $mock = static::isMock()
                    ? static::$resolvedInstance[$name]
                    : static::createFreshMockInstance();

        return $mock->shouldReceive(...func_get_args());
    }

    // 用于单元测试场景，创建一个新的 mock 实例
    protected static function createFreshMockInstance()
    {
        return tap(static::createMock(), function ($mock) {
            static::swap($mock);

            $mock->shouldAllowMockingProtectedMethods();
        });
    }

    // 用于单元测试场景，创建一个的 mock 实例
    protected static function createMock()
    {
        $class = static::getMockableClass();

        return $class ? Mockery::mock($class) : Mockery::mock();
    }

    // 用于单元测试场景，判断是否有创建过 mock 实例
    protected static function isMock()
    {
        $name = static::getFacadeAccessor();

        return isset(static::$resolvedInstance[$name]) &&
               static::$resolvedInstance[$name] instanceof MockInterface;
    }

    // 用于单元测试场景，获取容器当前所绑定的类名
    protected static function getMockableClass()
    {
        if ($root = static::getFacadeRoot()) {
            return get_class($root);
        }
    }

    // 替换 Facade 当前所绑定的实例
    public static function swap($instance)
    {
         static::$resolvedInstance[static::getFacadeAccessor()] = $instance;

        if (isset(static::$app)) {
            static::$app->instance(static::getFacadeAccessor(), $instance);
        }
    }

    // 获取 Facade 当前可以访问到的实例
    public static function getFacadeRoot()
    {
        return static::resolveFacadeInstance(static::getFacadeAccessor());
    }

    // 获取 Facade 在容器中的 $abstract，一般是字符串类型
    protected static function getFacadeAccessor()
    {
        throw new RuntimeException('Facade does not implement getFacadeAccessor method.');
    }

    // 从容器中解析出 Facade 对应的类实例
    protected static function resolveFacadeInstance($name)
    {
        if (is_object($name)) {
            return $name;
        }

        if (isset(static::$resolvedInstance[$name])) {
            return static::$resolvedInstance[$name];
        }

        return static::$resolvedInstance[$name] = static::$app[$name];
    }

    // 清除某个被解析过的实例
    public static function clearResolvedInstance($name)
    {
        unset(static::$resolvedInstance[$name]);
    }

    // 清除所有被解析过的实例
    public static function clearResolvedInstances()
    {
        static::$resolvedInstance = [];
    }

    // 获取容器
    public static function getFacadeApplication()
    {
        return static::$app;
    }

    // 设置容器
    public static function setFacadeApplication($app)
    {
        static::$app = $app;
    }

    // Facade 最核心的一个函数，
    // 当用户以调用静态函数的方式调用一个方法时，
    // 如果类没有定义这个方法，
    // 就会触发 __callStatic 这个魔术方法。
    // 这里，会从容器中解析出 Facade 对应的类实例，
    // 并调用对应的方法
    public static function __callStatic($method, $args)
    {
        $instance = static::getFacadeRoot();

        if (! $instance) {
            throw new RuntimeException('A facade root has not been set.');
        }

        return $instance->$method(...$args);
    }
}
```










