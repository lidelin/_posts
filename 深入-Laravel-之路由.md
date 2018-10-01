---
title: 深入 Laravel 之路由
date: 2018-06-11 21:58:14
tags: Laravel
---

路由主要是用来映射 url 和处理代码的关系，让我们给不同的 url 定义不同的处理代码的。Laravel 的路由非常强大，我们深入看看路由部分的代码。

# 整体流程

路由相关的代码，Laravel 会按以下顺序执行

- Illuminate\Routing\RoutingServiceProvider 
    路由器服务提供者，主要绑定路由器、URL 生成器等组件到服务容器
    
- Illuminate\Foundation\Support\Providers\RouteServiceProvider@boot
  App\Providers\RouteServiceProvider
    加载 routes 目录下的路由，并设定 prefix、middleware、group 等配置

- Illuminate\Foundation\Http\Kernel@dispatchToRouter
    把 request 分发给路由器
    
- Illuminate\Routing\Router@dispatch
  Illuminate\Routing\Router@dispatchToRoute
  Illuminate\Routing\Router@findRoute
  Illuminate\Routing\RouteCollection@match
  Illuminate\Routing\Router@runRoute
  Illuminate\Routing\Router@runRouteWithinStack  
  Illuminate\Routing\Route@run 
  找出路由的处理函数，并执行




# 代码分析

## Illuminate\Routing\RoutingServiceProvider

```php
class RoutingServiceProvider extends ServiceProvider
{
    public function register()
    {
        // 注册 router ，
        // 这里注册的 router 主要用来定义 url 和处理代码的关系
        $this->registerRouter();
        
        // 注册 url 生成器，
        // 比如把命名路由生成 URL
        $this->registerUrlGenerator();
        
        // 注册重定向器，
        // 主要用来做 url 重定向
        $this->registerRedirector();
        
        $this->registerPsrRequest();
        $this->registerPsrResponse();
        
        // 注册响应工厂，
        // 主要用来生成返回给用户的数据
        $this->registerResponseFactory();
        
        // 注册控制器分发器，
        // 主要用来调用控制器对应的方法
        $this->registerControllerDispatcher();
    }
    
    ...
}
```

## Illuminate\Foundation\Support\Providers\RouteServiceProvider@boot

```php
class RouteServiceProvider extends ServiceProvider
{
    ...
    
    public function boot()
    {
        // 设置 UrlGenerator 的命名空间
        $this->setRootControllerNamespace();

        // 判断 bootstrap/cache/routes.php 是否存在
        // 这个文件的作用是把路由缓存起来，减少加载路由的时间，提升整体性能
        if ($this->app->routesAreCached()) {
            // 如果 bootstrap/cache/routes.php ，则加载
            $this->loadCachedRoutes();
        } else {
            // 调用 App\Providers\RouteServiceProvider@map 来加载路由
            $this->loadRoutes();

            $this->app->booted(function () {
                // 更新路由命名查找表
                $this->app['router']->getRoutes()->refreshNameLookups();
                // 更新路由 action 查找表
                $this->app['router']->getRoutes()->refreshActionLookups();
            });
        }
    }
    
    ...
}
```


## Illuminate\Routing\Router http verbs 的处理

```php
class Router implements RegistrarContract, BindingRegistrar
{
    ...
    
    public function __construct(Dispatcher $events, Container $container = null)
    {
        // 事件分发器
        $this->events = $events;
        
        // 路由集合
        $this->routes = new RouteCollection;
        $this->container = $container ?: new Container;
    }
    
    public function get($uri, $action = null)
    {
        return $this->addRoute(['GET', 'HEAD'], $uri, $action);
    }
    
    其他的 http 方法，如 post、put、patch 等都是类似的，所以我们看一下 $this->addRoute() 的实现
    
    protected function addRoute($methods, $uri, $action)
    {
        return $this->routes->add($this->createRoute($methods, $uri, $action));
    }
    
    protected function createRoute($methods, $uri, $action)
    {
        // 先判断 $action 是不是 controller，
        // 如果是，会在 $action 添加 uses 和 controller 返回
        if ($this->actionReferencesController($action)) {
            $action = $this->convertToControllerAction($action);
        }

        // 检查路由组有没有设置 prifix 参数，有的话拼接上 prefix
        $route = $this->newRoute(
            $methods, $this->prefix($uri), $action
        );

        // 合并路由组设置的参数
        if ($this->hasGroupStack()) {
            $this->mergeGroupAttributesIntoRoute($route);
        }

        // 把 where 条件加到路由
        $this->addWhereClausesToRoute($route);

        return $route;
    }
    
    ...
}
```

## Illuminate\Routing\Router 请求被分发后的执行流程

```php
class Router implements RegistrarContract, BindingRegistrar
{
    ...

    public function dispatch(Request $request)
    {
        $this->currentRequest = $request;

        return $this->dispatchToRoute($request);
    }
    
    public function dispatchToRoute(Request $request)
    {
        // 运行 route 实例
        return $this->runRoute($request, $this->findRoute($request));
    }
    
    protected function findRoute($request)
    {
        // 查找和 Request 匹配的 Route 实例
        $this->current = $route = $this->routes->match($request);

        // 绑定 Route 实例
        $this->container->instance(Route::class, $route);

        return $route;
    }
    
    public function match(Request $request)
    {
        // 根据 http 方法如 GET、POST、PUT 获取路由
        $routes = $this->get($request->getMethod());

        // 匹配路由，
        // 如果能匹配到，直接返回路由
        // 这里返回的是 Illuminate\Routing\Route
        $route = $this->matchAgainstRoutes($routes, $request);

        // 如果 $route 非空，则和 Request 绑定
        if (! is_null($route)) {
            return $route->bind($request);
        }

        // 如果找不到匹配的 $route
        // 会尝试找一下其他的 http verbs 看看有没有相同的路由
        $others = $this->checkForAlternateVerbs($request);

        // 如果有的话，会报 MethodNotAllowed 405 错误
        if (count($others) > 0) {
            return $this->getRouteForMethods($request, $others);
        }

        // 如果都找不到，报 404 错误
        throw new NotFoundHttpException;
    }
    
    protected function runRoute(Request $request, Route $route)
    {
        // 设置路由的解析器
        $request->setRouteResolver(function () use ($route) {
            return $route;
        });

        // 分发 RouteMatched 事件
        $this->events->dispatch(new Events\RouteMatched($route, $request));

        // 运行 $route 实例并返回响应
        return $this->prepareResponse($request,
            $this->runRouteWithinStack($route, $request)
        );
    }
    
    protected function runRouteWithinStack(Route $route, Request $request)
    {
        // 判断是否需要执行中间件
        $shouldSkipMiddleware = $this->container->bound('middleware.disable') &&
                                $this->container->make('middleware.disable') === true;

        // 获取路由的中间件
        $middleware = $shouldSkipMiddleware ? [] : $this->gatherRouteMiddleware($route);

        // 执行 before middleware
        // 执行路由的 action
        // 执行 after middleware
        return (new Pipeline($this->container))
                        ->send($request)
                        ->through($middleware)
                        ->then(function ($request) use ($route) {
                            return $this->prepareResponse(
                                $request, $route->run()
                            );
                        });
    }
    
    ...
}
```

## Illuminate\Routing\Route

```php
class Route
{
    ...
    
    public function run()
    {
        $this->container = $this->container ?: new Container;

        try {
            // 判断是不是 controller action
            if ($this->isControllerAction()) {
                // 执行 controller action
                return $this->runController();
            }
            
            // 直接执行 action 
            return $this->runCallable();
        } catch (HttpResponseException $e) {
            return $e->getResponse();
        }
    }
    
    protected function runCallable()
    {
        $callable = $this->action['uses'];

        // 解析 callable 所需要的依赖，参数，
        // 然后执行
        return $callable(...array_values($this->resolveMethodDependencies(
            $this->parametersWithoutNulls(), new ReflectionFunction($this->action['uses'])
        )));
    }
    
    protected function runController()
    {
        // 获取 controller 和方法
        // 通过 controller 分发器分发执行
        return $this->controllerDispatcher()->dispatch(
            $this, $this->getController(), $this->getControllerMethod()
        );
    }
    
    ...
}
```

## Illuminate\Routing\ControllerDispatcher

```php
class ControllerDispatcher implements ControllerDispatcherContract
{
    ...
    
    public function dispatch(Route $route, $controller, $method)
    {
        // 解析 controller 和方法所需要的依赖
        $parameters = $this->resolveClassMethodDependencies(
            $route->parametersWithoutNulls(), $controller, $method
        );

        // 执行 controller 的 callAction 方法
        // callAction 回调用 call_user_func_array 来执行方法
        if (method_exists($controller, 'callAction')) {
            return $controller->callAction($method, $parameters);
        }

        // 执行 controller 的方法
        return $controller->{$method}(...array_values($parameters));
    }
    
    ...
}

```
