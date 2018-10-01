---
title: 深入 Laravel 之缓存系统
date: 2018-07-04 23:53:22
tags: Laravel
---

Laravel 为各种后端缓存系统提供了丰富统一的 API，这节我们来看看 Laravel 的缓存系统。

# 缓存系统的 interface

缓存系统的主要接口有两个：
- Illuminate\Contracts\Cache\Repository
- Illuminate\Contracts\Cache\Store

## Repository 

```php
<?php

namespace Illuminate\Contracts\Cache;

use Closure;
use Psr\SimpleCache\CacheInterface;

interface Repository extends CacheInterface
{
    /**
     * 判断缓存中是否存在 $key 的数据
     */
    public function has($key);

    /**
     * 通过 $key 从缓存中获取数据
     */
    public function get($key, $default = null);

    /**
     * 通过 key 从缓存中获取数据出来后，把缓存中的数据删掉
     */
    public function pull($key, $default = null);

    /**
     * 把数据存到缓存
     */
    public function put($key, $value, $minutes);

    /**
     * 如果缓存中不存在 $key，把数据存到缓存
     */
    public function add($key, $value, $minutes);

    /**
     * 增加缓存中某个项目的值
     */
    public function increment($key, $value = 1);

    /**
     * 减小缓存中某个项目的值
     */
    public function decrement($key, $value = 1);

    /**
     * 把某数据永久存在缓存中
     */
    public function forever($key, $value);

    /**
     * 从缓存中获取某数据，如果不存在，回调用 $callback 来获取这个数据，并存储到缓存
     */
    public function remember($key, $minutes, Closure $callback);

    /**
     * 从缓存中获取某数据，如果不存在，回调用 $callback 来获取这个数据，并永久地存储到缓存
     */
    public function sear($key, Closure $callback);

    /**
     * 从缓存中获取某数据，如果不存在，回调用 $callback 来获取这个数据，并永久地存储到缓存
     */
    public function rememberForever($key, Closure $callback);

    /**
     * 从缓存中移除某项数据
     */
    public function forget($key);

    /**
     * 获取 store 实例
     */
    public function getStore();
}
```

Repository 接口继承了 Psr\SimpleCache\CacheInterface，我们再来看看这个接口。

```php
<?php

namespace Psr\SimpleCache;

interface CacheInterface
{
    /**
     * 通过 $key 从缓存中获取数据
     */
    public function get($key, $default = null);

    /**
     * 把数据存到缓存中
     */
    public function set($key, $value, $ttl = null);

    /**
     * 通过 $key 从缓存中删除某项数据
     */
    public function delete($key);

    /**
     * 清除缓存中所有数据
     */
    public function clear();

    /**
     * 通过 $keys 从缓存中获取多个数据
     */
    public function getMultiple($keys, $default = null);

    /**
     * 存储一系列数据到缓存
     */
    public function setMultiple($values, $ttl = null);

    /**
     * 通过 $keys 从缓存中删除多个数据
     */
    public function deleteMultiple($keys);

    /**
     * 判断缓存中是否存在 $key 的数据
     */
    public function has($key);
}

```

## Store

```php
<?php

namespace Illuminate\Contracts\Cache;

interface Store
{
    /**
     * 通过 $key 从缓存中获取数据
     */
    public function get($key);

    /**
     * 通过 $keys 从缓存中获取多个数据
     */
    public function many(array $keys);

    /**
     * 把数据存到缓存中
     */
    public function put($key, $value, $minutes);

    /**
     * 存储一系列数据到缓存
     */
    public function putMany(array $values, $minutes);

    /**
     * 增加缓存中某个项目的值
     */
    public function increment($key, $value = 1);

    /**
     * 减小缓存中某个项目的值
     */
    public function decrement($key, $value = 1);

    /**
     * 把某个数据永久地存在缓存中
     */
    public function forever($key, $value);

    /**
     * 通过 $key 删除缓存中某项数据
     */
    public function forget($key);

    /**
     * 清除缓存中所有数据
     */
    public function flush();

    /**
     * 获取缓存键的前缀
     */
    public function getPrefix();
}

```

# Store 和 Repository 区别

看了 Illuminate\Contracts\Cache\Store 和 Illuminate\Contracts\Cache\Repository，大家肯定也发现了这两个接口很相似。那这两个接口的区别是什么呢？我们先来看看实现这两个接口的类。

![](https://i.loli.net/2018/07/08/5b41d8a7d6590.jpeg)
![](https://i.loli.net/2018/07/08/5b41d8a85a044.jpeg)

可以看到，Store 是对缓存后端，比如 Redis，Memcached 等的封装；Repository 是供用户使用的统一的 API 封装。


# 整体分析

缓存系统主要有以下几个角色：
- Cache 服务提供者，负责和 Laravel 建立连接
- Cache Manager，负责管理整个缓存系统
- Driver，即 store，负责对缓存后端，比如 Redis，Memcached 等的封装

## Cache 服务提供者

```php
<?php

namespace Illuminate\Cache;

use Illuminate\Support\ServiceProvider;

class CacheServiceProvider extends ServiceProvider
{
    ...

    public function register()
    {
        $this->app->singleton('cache', function ($app) {
            return new CacheManager($app);
        });

        $this->app->singleton('cache.store', function ($app) {
            return $app['cache']->driver();
        });

        $this->app->singleton('memcached.connector', function () {
            return new MemcachedConnector;
        });
    }

    ...
}

```

主要是把 cache manager 和 cache driver 绑定到 Laravel 的服务容器。memcached connector 主要用于 Memcached driver。

## Cache Manager

```php
<?php

namespace Illuminate\Cache;

use Closure;
use InvalidArgumentException;
use Illuminate\Contracts\Cache\Store;
use Illuminate\Contracts\Cache\Factory as FactoryContract;
use Illuminate\Contracts\Events\Dispatcher as DispatcherContract;

class CacheManager implements FactoryContract
{
    ...
    
    /**
     * Get a cache store instance by name.
     *
     * @param  string|null  $name
     * @return \Illuminate\Contracts\Cache\Repository
     */
    public function store($name = null)
    {
        $name = $name ?: $this->getDefaultDriver();

        return $this->stores[$name] = $this->get($name);
    }
    
    ...

    /**
     * Resolve the given store.
     *
     * @param  string  $name
     * @return \Illuminate\Contracts\Cache\Repository
     *
     * @throws \InvalidArgumentException
     */
    protected function resolve($name)
    {
        $config = $this->getConfig($name);

        if (is_null($config)) {
            throw new InvalidArgumentException("Cache store [{$name}] is not defined.");
        }

        if (isset($this->customCreators[$config['driver']])) {
            return $this->callCustomCreator($config);
        } else {
            $driverMethod = 'create'.ucfirst($config['driver']).'Driver';

            if (method_exists($this, $driverMethod)) {
                return $this->{$driverMethod}($config);
            } else {
                throw new InvalidArgumentException("Driver [{$config['driver']}] is not supported.");
            }
        }
    }

    /**
     * Create an instance of the Memcached cache driver.
     *
     * @param  array  $config
     * @return \Illuminate\Cache\MemcachedStore
     */
    protected function createMemcachedDriver(array $config)
    {
        $prefix = $this->getPrefix($config);

        $memcached = $this->app['memcached.connector']->connect(
            $config['servers'],
            $config['persistent_id'] ?? null,
            $config['options'] ?? [],
            array_filter($config['sasl'] ?? [])
        );

        return $this->repository(new MemcachedStore($memcached, $prefix));
    }

    /**
     * Create an instance of the Redis cache driver.
     *
     * @param  array  $config
     * @return \Illuminate\Cache\RedisStore
     */
    protected function createRedisDriver(array $config)
    {
        $redis = $this->app['redis'];

        $connection = $config['connection'] ?? 'default';

        return $this->repository(new RedisStore($redis, $this->getPrefix($config), $connection));
    }

    /**
     * Register a custom driver creator Closure.
     *
     * @param  string    $driver
     * @param  \Closure  $callback
     * @return $this
     */
    public function extend($driver, Closure $callback)
    {
        $this->customCreators[$driver] = $callback->bindTo($this, $this);

        return $this;
    }

    /**
     * Dynamically call the default driver instance.
     *
     * @param  string  $method
     * @param  array   $parameters
     * @return mixed
     */
    public function __call($method, $parameters)
    {
        return $this->store()->$method(...$parameters);
    }
    
    ...
}

```

- __call 中直接调用 $this->store() 的方法，$this->store() 会返回一个 \Illuminate\Contracts\Cache\Repository 实例，所以 cache manager 拥有 repository 的所有功能
- resolve() 方法负责根据 cache 的配置，创建不同 driver 的 repository 实例
- extend() 可以扩展缓存系统

## Driver

缓存 driver 部分，就是实现 Illuminate\Contracts\Cache\Store 接口，对缓存后端的封装。Laravel 目前支持 file，redis，apc，database，array 的缓存。
以 redis 为例，我们看一下 public function get($key) 的实现
```php
<?php

namespace Illuminate\Cache;

use Illuminate\Contracts\Cache\Store;
use Illuminate\Contracts\Redis\Factory as Redis;

class RedisStore extends TaggableStore implements Store
{
    ...
    
    /**
     * Retrieve an item from the cache by key.
     *
     * @param  string|array  $key
     * @return mixed
     */
    public function get($key)
    {
        $value = $this->connection()->get($this->prefix.$key);

        return ! is_null($value) ? $this->unserialize($value) : null;
    }
    
    ...
}
```

其实就是用 \Redis 的方法来实现的。





