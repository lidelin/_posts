---
title: 深入 Laravel 之服务提供者
date: 2018-06-01 21:33:40
tags: Laravel
---

服务提供者是 Laravel 的 “引导” 中心。我们自己的程序和 Laravel 的核心程序，都是通过服务提供者引导启动的。

> “引导”：注册容器绑定，事件监听器，中间件，路由等。

# 编写服务提供者
```bash
php artisan make:provider RiakServiceProvider
```
这个命令会在 app/Providers 生成一个 RiakServiceProvider

```php
<?php

namespace App\Providers;

use Illuminate\Support\ServiceProvider;

class RiakServiceProvider extends ServiceProvider
{
    /**
     * Bootstrap services.
     *
     * @return void
     */
    public function boot()
    {
        //
    }

    /**
     * Register services.
     *
     * @return void
     */
    public function register()
    {
        //
    }
}
```

## register 方法

在 register() 中，我们只能做注册绑定容器的事情，像注册事件监听，注册路由这些都不能在 register() 中处理。**因为 register() 被调用的时候，其他模块可能还没被加载。**

## boot 方法

**当所有服务提供者的 register() 方法被调用之后，就会调用 boot() 方法。** 在 boot() 方法我们就可以注册事件监听，路由等需要用到其他模块的事情了。

# 注册服务提供者

- 在 app/config/app.php 配置文件中，有一个 providers 数组。把我们的 provider 加到这个数组即可
- laravel 5.5 之后支持 package discovery 。如果我们的 service providor 是以 composer 包的形式提供的，可以在 composer.json 参考以下加上一段代码：
```json
"extra": {
    "laravel": {
        "providers": [
            "Barryvdh\\Debugbar\\ServiceProvider"
        ],
        "aliases": {
            "Debugbar": "Barryvdh\\Debugbar\\Facade"
        }
    }
}
```

# 延迟加载服务提供者

如果你的服务提供者只是注册绑定到服务容器，可以选择使用延迟加载。延迟加载会在真正需要使用这个绑定的时候才运行，可以帮助我们提高性能。

延迟加载服务提供者，只需要把 $defer 设为 true ，并且定义 provides() 方法。provides() 方法负责返回 register() 中注册的容器绑定。

```php
<?php

namespace App\Providers;

use Riak\Connection;
use Illuminate\Support\ServiceProvider;

class RiakServiceProvider extends ServiceProvider
{
    /**
     * Indicates if loading of the provider is deferred.
     *
     * @var bool
     */
    protected $defer = true;

    /**
     * Register the service provider.
     *
     * @return void
     */
    public function register()
    {
        $this->app->singleton(Connection::class, function ($app) {
            return new Connection($app['config']['riak']);
        });
    }

    /**
     * Get the services provided by the provider.
     *
     * @return array
     */
    public function provides()
    {
        return [Connection::class];
    }
}
```

# 代码分析

## 流程
1. 在 Illuminate\Foundation\Application 的构造函数中，会先注册 event、log 和 routing 三个服务提供者
2. 在 sendRequestThroughRouter 中，会调用 Illuminate\Foundation\Application@bootstrapWith 引导
    ```php
    protected $bootstrappers = [
            \Illuminate\Foundation\Bootstrap\LoadEnvironmentVariables::class,
            \Illuminate\Foundation\Bootstrap\LoadConfiguration::class,
            \Illuminate\Foundation\Bootstrap\HandleExceptions::class,
            \Illuminate\Foundation\Bootstrap\RegisterFacades::class,
            \Illuminate\Foundation\Bootstrap\RegisterProviders::class,
            \Illuminate\Foundation\Bootstrap\BootProviders::class,
        ];
    ```

    以上六个 bootstrappers 。其中 
    \Illuminate\Foundation\Bootstrap\RegisterProviders::class 会调用所有的非延迟加载的服务提供者的 register()，把延迟加载的服务提供者记录到 Application 的 $deferredServices 中；
    \Illuminate\Foundation\Bootstrap\BootProviders::class 会调用所有的非延迟加载的服务提供者的 boot()。

## Illuminate\Foundation\Bootstrap\RegisterProviders

```php
class RegisterProviders
{
    // 调用 Application@registerConfiguredProviders
    public function bootstrap(Application $app)
    {
        $app->registerConfiguredProviders();
    }
}

public function registerConfiguredProviders()
{
    // 获取 app/config/app.php 中的 providers 数组
    $providers = Collection::make($this->config['app.providers'])
                    ->partition(function ($provider) {
                        return Str::startsWith($provider, 'Illuminate\\');
                    });

    // 把 package discovery 注册的服务提供者整合进 $providers
    $providers->splice(1, 0, [$this->make(PackageManifest::class)->providers()]);

    // 注册所有的服务提供者
    (new ProviderRepository($this, new Filesystem, $this->getCachedServicesPath()))
                ->load($providers->collapse()->toArray());
}

class ProviderRepository
{
    ...
    
    public function load(array $providers)
    {
        // 加载 bootstrap/cache/services.php
        $manifest = $this->loadManifest();

        // 如果 $manifest 为空或者和 $providers 不一样，会重新生成 bootstrap/cache/services.php
        if ($this->shouldRecompile($manifest, $providers)) {
            $manifest = $this->compileManifest($providers);
        }

        // 注册监听事件
        foreach ($manifest['when'] as $provider => $events) {
            $this->registerLoadEvents($provider, $events);
        }

        // 调用服务提供者的 register()
        foreach ($manifest['eager'] as $provider) {
            $this->app->register($provider);
        }

        // 记录延迟加载的服务提供者
        $this->app->addDeferredServices($manifest['deferred']);
    }
    
    ...
}
```

## Illuminate\Foundation\Bootstrap\BootProviders

```php
class BootProviders
{
    public function bootstrap(Application $app)
    {
        $app->boot();
    }
}

class Application
{
    ...
    
    public function boot()
    {
        if ($this->booted) {
            return;
        }

        $this->fireAppCallbacks($this->bootingCallbacks);

        // 调用所有服务提供者的 boot() 方法
        array_walk($this->serviceProviders, function ($p) {
            $this->bootProvider($p);
        });

        $this->booted = true;

        $this->fireAppCallbacks($this->bootedCallbacks);
    }
    
    ...
}
```

## 延迟加载的服务提供者的处理

```php
class Application
{
    ...
    
    public function make($abstract, array $parameters = [])
    {
        $abstract = $this->getAlias($abstract);

        // 解析容器绑定的时候，先判断是不是延迟加载的服务提供者
        if (isset($this->deferredServices[$abstract]) && ! isset($this->instances[$abstract])) {
            $this->loadDeferredProvider($abstract);
        }

        return parent::make($abstract, $parameters);
    }
    
    public function loadDeferredProvider($service)
    {
        if (! isset($this->deferredServices[$service])) {
            return;
        }

        $provider = $this->deferredServices[$service];

        // 如果服务提供者还没被加载和注册，就加载和注册，同时把该服务提供者从 $this->deferredServices 移除
        if (! isset($this->loadedProviders[$provider])) {
            $this->registerDeferredProvider($provider, $service);
        }
    }
    
    ...
}
```
















