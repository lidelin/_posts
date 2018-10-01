---
title: 深入 Laravel 之用户认证系统
date: 2018-07-29 15:49:18
tags: Laravel
---

Laravel 的用户认证系统是由“守卫”和“提供者”组成的。
守卫定义了该如何认证每个请求的用户。例如，Laravel 的 session 守卫用 session 存储和 cookies 来维护状态。
提供者定义了怎样从持久化的存储中检索用户。Laravel 支持从 Eloquent 和查询构造器检索用户。

# 用户认证系统主要的 Interface

主要的接口有以下几个：
- Illuminate\Contracts\Auth\Guard
- Illuminate\Contracts\Auth\UserProvider

## Guard
```php
<?php

namespace Illuminate\Contracts\Auth;

interface Guard
{
    /**
     * 判断当前用户是否已经认证过
     */
    public function check();

    /**
     * 判断当前用户是不是游客(即没登录)
     */
    public function guest();

    /**
     * 获取当前已经认证的用户
     * @return \Illuminate\Contracts\Auth\Authenticatable|null
     */
    public function user();

    /**
     * 获取当前已经认证的用户的 ID
     */
    public function id();

    /**
     * 验证一个用户的凭证
     */
    public function validate(array $credentials = []);

    /**
     * 设置认证的用户
     */
    public function setUser(Authenticatable $user);
}

```

## UserProvider
```php
<?php

namespace Illuminate\Contracts\Auth;

interface UserProvider
{
    /**
     * 通过用户的唯一标识符检索用户
     */
    public function retrieveById($identifier);

    /**
     * 通过用户的唯一标识符和“记住我”令牌来检索用户
     */
    public function retrieveByToken($identifier, $token);

    /**
     * 更新某个用户“记住我”的令牌
     */
    public function updateRememberToken(Authenticatable $user, $token);

    /**
     * 通过凭证检索用户
     */
    public function retrieveByCredentials(array $credentials);

    /**
     * 验证用户提供的凭据
     */
    public function validateCredentials(Authenticatable $user, array $credentials);
}
```

# Laravel 自带的 Guard 和 UserProvider 实现

## Guard
![](https://i.loli.net/2018/07/30/5b5df382d00c5.jpeg)

## UserProvider
![](https://i.loli.net/2018/07/30/5b5df3a3f2db8.jpeg)


# 用户认证系统的中间件

Laravel 的用户认证是放在中间件处理的。

```php
<?php

namespace Illuminate\Auth\Middleware;

use Closure;
use Illuminate\Auth\AuthenticationException;
use Illuminate\Contracts\Auth\Factory as Auth;

class Authenticate
{
    /**
     * The authentication factory instance.
     *
     * @var \Illuminate\Contracts\Auth\Factory
     */
    protected $auth;

    /**
     * Create a new middleware instance.
     *
     * @param  \Illuminate\Contracts\Auth\Factory  $auth
     * @return void
     */
    public function __construct(Auth $auth)
    {
        $this->auth = $auth;
    }

    /**
     * Handle an incoming request.
     *
     * @param  \Illuminate\Http\Request  $request
     * @param  \Closure  $next
     * @param  string[]  ...$guards
     * @return mixed
     *
     * @throws \Illuminate\Auth\AuthenticationException
     */
    public function handle($request, Closure $next, ...$guards)
    {
        $this->authenticate($guards);

        return $next($request);
    }

    /**
     * Determine if the user is logged in to any of the given guards.
     *
     * @param  array  $guards
     * @return void
     *
     * @throws \Illuminate\Auth\AuthenticationException
     */
    protected function authenticate(array $guards)
    {
        if (empty($guards)) {
            return $this->auth->authenticate();
        }

        foreach ($guards as $guard) {
            if ($this->auth->guard($guard)->check()) {
                return $this->auth->shouldUse($guard);
            }
        }

        throw new AuthenticationException('Unauthenticated.', $guards);
    }
}
```

最终就是调用守卫的 authenticate() 来认证并获取用户。


# 整体分析

用户认证系统主要有以下的角色：
- 服务提供者： Illuminate\Auth\AuthServiceProvider，主要把用户认证系统所需的实现绑定到容器，负责和 Laravel 建立连接
- 管理员：Illuminate\Auth\AuthManager，负责管理守卫和提供者
- 守卫，负责认证用户
- 用户提供者，负责检索用户并提供认证过的用户的信息

## 用户认证系统的服务提供者

```php
<?php

namespace Illuminate\Auth;

...

class AuthServiceProvider extends ServiceProvider
{
    /**
     * Register the service provider.
     *
     * @return void
     */
    public function register()
    {
        $this->registerAuthenticator();

        $this->registerUserResolver();

        $this->registerAccessGate();

        $this->registerRequestRebindHandler();
    }
    
    ...
}
```

$this->registerAuthenticator()，注册认证者，即认证系统中的管理员；$this->registerUserResolver()，注册用户解析器；$this->registerAccessGate()，属于权限验证那部分的，这里暂不讨论；$this->registerRequestRebindHandler()，给 Request 注册用户解析器。

## 认证系统的管理员

```php
<?php

namespace Illuminate\Auth;

use Closure;
use InvalidArgumentException;
use Illuminate\Contracts\Auth\Factory as FactoryContract;

class AuthManager implements FactoryContract
{
    use CreatesUserProviders;

    ...

    /**
     * Attempt to get the guard from the local cache.
     *
     * @param  string  $name
     * @return \Illuminate\Contracts\Auth\Guard|\Illuminate\Contracts\Auth\StatefulGuard
     */
    public function guard($name = null)
    {
        $name = $name ?: $this->getDefaultDriver();

        return $this->guards[$name] ?? $this->guards[$name] = $this->resolve($name);
    }

    /**
     * Resolve the given guard.
     *
     * @param  string  $name
     * @return \Illuminate\Contracts\Auth\Guard|\Illuminate\Contracts\Auth\StatefulGuard
     *
     * @throws \InvalidArgumentException
     */
    protected function resolve($name)
    {
        $config = $this->getConfig($name);

        if (is_null($config)) {
            throw new InvalidArgumentException("Auth guard [{$name}] is not defined.");
        }

        if (isset($this->customCreators[$config['driver']])) {
            return $this->callCustomCreator($name, $config);
        }

        $driverMethod = 'create'.ucfirst($config['driver']).'Driver';

        if (method_exists($this, $driverMethod)) {
            return $this->{$driverMethod}($name, $config);
        }

        throw new InvalidArgumentException("Auth driver [{$config['driver']}] for guard [{$name}] is not defined.");
    }
    
    ...
    
    public function createSessionDriver($name, $config)
    {
        ...
    }
    
    /**
     * Create a token based authentication guard.
     *
     * @param  string  $name
     * @param  array  $config
     * @return \Illuminate\Auth\TokenGuard
     */
    public function createTokenDriver($name, $config)
    {
        // The token guard implements a basic API token based guard implementation
        // that takes an API token field from the request and matches it to the
        // user in the database or another persistence layer where users are.
        $guard = new TokenGuard(
            $this->createUserProvider($config['provider'] ?? null),
            $this->app['request']
        );

        $this->app->refresh('request', $guard, 'setRequest');

        return $guard;
    }

    /**
     * Register a custom driver creator Closure.
     *
     * @param  string  $driver
     * @param  \Closure  $callback
     * @return $this
     */
    public function extend($driver, Closure $callback)
    {
        $this->customCreators[$driver] = $callback;

        return $this;
    }

    ...
    
    public function createUserProvider($provider = null)
    {
        if (is_null($config = $this->getProviderConfiguration($provider))) {
            return;
        }

        if (isset($this->customProviderCreators[$driver = ($config['driver'] ?? null)])) {
            return call_user_func(
                $this->customProviderCreators[$driver], $this->app, $config
            );
        }

        switch ($driver) {
            case 'database':
                return $this->createDatabaseProvider($config);
            case 'eloquent':
                return $this->createEloquentProvider($config);
            default:
                throw new InvalidArgumentException(
                    "Authentication user provider [{$driver}] is not defined."
                );
        }
    }
    
    protected function createEloquentProvider($config)
    {
        return new EloquentUserProvider($this->app['hash'], $config['model']);
    }
}

```

通过 guard($name = null) 可以获取守卫，当在 $this->guards 没找到时，会调用 resolve($name) 来解析。对于 Laravel 自带的守卫，会调用 createSessionDriver 和 createTokenDriver 来创建，最终就是创建 Illuminate\Auth\TokenGuard 和 Illuminate\Auth\SessionGuard，并绑定到 Request。

在创建守卫时，要先创建用户提供者。对于 Laravel 自带的用户提供者，会调用 createDatabaseProvider 和 createEloquentProvider 来创建，最终就是创建 Illuminate\Auth\EloquentUserProvider 和 Illuminate\Auth\DatabaseUserProvider。

## 守卫

Laravel 提供了 RequestGuard、TokenGuard 和 SessionGuard，我们以 TokenGuard 为例。

```php
namespace Illuminate\Auth;

use Illuminate\Http\Request;
use Illuminate\Contracts\Auth\Guard;
use Illuminate\Contracts\Auth\UserProvider;

class TokenGuard implements Guard
{
    use GuardHelpers;
    
    ...
    
    public function authenticate()
    {
        if (! is_null($user = $this->user())) {
            return $user;
        }

        throw new AuthenticationException;
    }
    
    ...
    
    public function user()
    {
        // If we've already retrieved the user for the current request we can just
        // return it back immediately. We do not want to fetch the user data on
        // every call to this method because that would be tremendously slow.
        if (! is_null($this->user)) {
            return $this->user;
        }

        $user = null;

        $token = $this->getTokenForRequest();

        if (! empty($token)) {
            $user = $this->provider->retrieveByCredentials(
                [$this->storageKey => $token]
            );
        }

        return $this->user = $user;
    }
    
    ...
}
```

我们通过中间件的代码可以知道，最终会调用守卫的 authenticate() 来对用户认证。从上面的代码可以看出，authenticate() 会调用 user() 方法来获取用户，最终就是从用户提供者获取用户。如果能获取到用户，认证就通过；否则，认证失败。

## 用户提供者

Laravel 提供了 DatabaseUserProvider 和 EloquentUserProvider，我们来看看 EloquentUserProvider 的代码。

```php
<?php

namespace Illuminate\Auth;

...

class EloquentUserProvider implements UserProvider
{
    ...

    /**
     * Retrieve a user by their unique identifier.
     *
     * @param  mixed  $identifier
     * @return \Illuminate\Contracts\Auth\Authenticatable|null
     */
    public function retrieveById($identifier)
    {
        $model = $this->createModel();

        return $model->newQuery()
            ->where($model->getAuthIdentifierName(), $identifier)
            ->first();
    }

    /**
     * Retrieve a user by their unique identifier and "remember me" token.
     *
     * @param  mixed  $identifier
     * @param  string  $token
     * @return \Illuminate\Contracts\Auth\Authenticatable|null
     */
    public function retrieveByToken($identifier, $token)
    {
        $model = $this->createModel();

        $model = $model->where($model->getAuthIdentifierName(), $identifier)->first();

        if (! $model) {
            return null;
        }

        $rememberToken = $model->getRememberToken();

        return $rememberToken && hash_equals($rememberToken, $token) ? $model : null;
    }

    /**
     * Update the "remember me" token for the given user in storage.
     *
     * @param  \Illuminate\Contracts\Auth\Authenticatable  $user
     * @param  string  $token
     * @return void
     */
    public function updateRememberToken(UserContract $user, $token)
    {
        $user->setRememberToken($token);

        $timestamps = $user->timestamps;

        $user->timestamps = false;

        $user->save();

        $user->timestamps = $timestamps;
    }

    /**
     * Retrieve a user by the given credentials.
     *
     * @param  array  $credentials
     * @return \Illuminate\Contracts\Auth\Authenticatable|null
     */
    public function retrieveByCredentials(array $credentials)
    {
        if (empty($credentials) ||
           (count($credentials) === 1 &&
            array_key_exists('password', $credentials))) {
            return;
        }

        // First we will add each credential element to the query as a where clause.
        // Then we can execute the query and, if we found a user, return it in a
        // Eloquent User "model" that will be utilized by the Guard instances.
        $query = $this->createModel()->newQuery();

        foreach ($credentials as $key => $value) {
            if (Str::contains($key, 'password')) {
                continue;
            }

            if (is_array($value) || $value instanceof Arrayable) {
                $query->whereIn($key, $value);
            } else {
                $query->where($key, $value);
            }
        }

        return $query->first();
    }

    /**
     * Validate a user against the given credentials.
     *
     * @param  \Illuminate\Contracts\Auth\Authenticatable  $user
     * @param  array  $credentials
     * @return bool
     */
    public function validateCredentials(UserContract $user, array $credentials)
    {
        $plain = $credentials['password'];

        return $this->hasher->check($plain, $user->getAuthPassword());
    }

    /**
     * Create a new instance of the model.
     *
     * @return \Illuminate\Database\Eloquent\Model
     */
    public function createModel()
    {
        $class = '\\'.ltrim($this->model, '\\');

        return new $class;
    }

    ...
}

```

通过上面的代码我们可以看到，用户提供者的实现跟我们平时使用 Eloquent 查询数据相差不多，主要通过用户 ID、token 或者 email 和密码来查询用户信息。



