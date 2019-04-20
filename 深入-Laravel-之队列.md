---
title: 深入 Laravel 之队列
date: 2018-06-22 23:42:13
tags: Laravel
---

# 前言
Laravel 的队列为多种不同的队列后台提供了统一的 API，比如 Beanstalk， Amazon SQS，Redis，还有关系型数据库。接下来让我们通过这篇文章深入地了解一下 Laravel 的队列。

<!--more-->

# 基本用法
## 定义 QUEUE_DRIVER
修改 .env 中的 QUEUE_DRIVER 即可，值可以是 sync、database、beanstalkd、sqs、redis，我们以 database 为例。（要 php artisan queue:table 添加 jobs 表）

## 创建 Job
```bash
php artisan make:job ProcessPodcast
```

```php
<?php

namespace App\Jobs;

use Illuminate\Bus\Queueable;
use Illuminate\Queue\SerializesModels;
use Illuminate\Queue\InteractsWithQueue;
use Illuminate\Contracts\Queue\ShouldQueue;
use Illuminate\Foundation\Bus\Dispatchable;

class ProcessPodcast implements ShouldQueue
{
    use Dispatchable, InteractsWithQueue, Queueable, SerializesModels;

    /**
     * Create a new job instance.
     *
     * @return void
     */
    public function __construct()
    {
        //
    }

    /**
     * Execute the job.
     *
     * @return void
     */
    public function handle()
    {
        // Process uploaded podcast...
    }
}
```

1. job 必须 implement Illuminate\Contracts\Queue\ShouldQueue 这个 interface，这样 Laravel 会把这个 job 放到队列来执行
2. 当 job 被执行的时候，handle 方法会被调用

## 分发 Job
```php
ProcessPodcast::dispatch();
```

## 队列 worker
```bash
php artisan queue:work
```
以上命令会处理队列里的 job，一般都要 Supervisor 来监控，保证 worker 进程一直在跑。


# 代码分析

## Illuminate\Queue\QueueServiceProvider

```php
<?php

namespace Illuminate\Queue;

use Illuminate\Support\ServiceProvider;
use Illuminate\Queue\Connectors\SqsConnector;
use Illuminate\Queue\Connectors\NullConnector;
use Illuminate\Queue\Connectors\SyncConnector;
use Illuminate\Queue\Connectors\RedisConnector;
use Illuminate\Contracts\Debug\ExceptionHandler;
use Illuminate\Queue\Connectors\DatabaseConnector;
use Illuminate\Queue\Failed\NullFailedJobProvider;
use Illuminate\Queue\Connectors\BeanstalkdConnector;
use Illuminate\Queue\Failed\DatabaseFailedJobProvider;

class QueueServiceProvider extends ServiceProvider
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
        // 注册队列管理员，
        // 即 Illuminate\Queue\QueueManager，
        // 具体分析看下个小节
        $this->registerManager();

        // 绑定 queue.connection
        $this->registerConnection();

        // 绑定 queue.worker，
        // Laravel 绑定了 Illuminate\Queue\Worker
        $this->registerWorker();

        // 绑定队列监听器
        $this->registerListener();

        // 绑定记录执行失败的 job 的处理
        $this->registerFailedJobServices();
    }
    
    /**
     * 队列管理员中用到的连接器都是在这里配置的
     */
    public function registerConnectors($manager)
    {
        foreach (['Null', 'Sync', 'Database', 'Redis', 'Beanstalkd', 'Sqs'] as $connector) {
            $this->{"register{$connector}Connector"}($manager);
        }
    }

    ...
}

```

## Illuminate\Queue\QueueManager

QueueManager 负责添加队列各种事件的监听，创建队列连接实例。
我们主要看一下创建队列连接实例的代码。

```php
<?php

namespace Illuminate\Queue;

use Closure;
use InvalidArgumentException;
use Illuminate\Contracts\Queue\Factory as FactoryContract;
use Illuminate\Contracts\Queue\Monitor as MonitorContract;

/**
 * @mixin \Illuminate\Contracts\Queue\Queue
 */
class QueueManager implements FactoryContract, MonitorContract
{
    ...
    
    public function connection($name = null)
    {
        $name = $name ?: $this->getDefaultDriver();

        // 队列连接实例如果已经解析过则直接获取
        if (! isset($this->connections[$name])) {
            // 解析队列连接实例
            $this->connections[$name] = $this->resolve($name);

            $this->connections[$name]->setContainer($this->app);
        }

        return $this->connections[$name];
    }
    
    /**
     * 解析一个队列连接实例
     */
    protected function resolve($name)
    {
        // 获取队列的配置
        $config = $this->getConfig($name);

        // 获取对应的连接器，并设置队列名字后返回
        // 连接器会在 Illuminate\Queue\QueueServiceProvider 注册进来
        return $this->getConnector($config['driver'])
                        ->connect($config)
                        ->setConnectionName($name);
    }
    
    ...

}
```

## Illuminate\Queue\Console\WorkCommand

WorkCommand 主要实现了 php artisan queue:work 命令。

```php
<?php

namespace Illuminate\Queue\Console;

use Illuminate\Queue\Worker;
use Illuminate\Support\Carbon;
use Illuminate\Console\Command;
use Illuminate\Contracts\Queue\Job;
use Illuminate\Queue\WorkerOptions;
use Illuminate\Queue\Events\JobFailed;
use Illuminate\Queue\Events\JobProcessed;
use Illuminate\Queue\Events\JobProcessing;

class WorkCommand extends Command
{
    /**
     * The console command name.
     *
     * @var string
     */
    protected $signature = 'queue:work
                            {connection? : The name of the queue connection to work}
                            {--queue= : The names of the queues to work}
                            {--daemon : Run the worker in daemon mode (Deprecated)}
                            {--once : Only process the next job on the queue}
                            {--delay=0 : The number of seconds to delay failed jobs}
                            {--force : Force the worker to run even in maintenance mode}
                            {--memory=128 : The memory limit in megabytes}
                            {--sleep=3 : Number of seconds to sleep when no job is available}
                            {--timeout=60 : The number of seconds a child process can run}
                            {--tries=0 : Number of times to attempt a job before logging it failed}';

    /**
     * The console command description.
     *
     * @var string
     */
    protected $description = 'Start processing jobs on the queue as a daemon';

    /**
     * 队列 worker 实例
     * @var \Illuminate\Queue\Worker
     */
    protected $worker;

    // 注入队列 worker 实例
    public function __construct(Worker $worker)
    {
        parent::__construct();
        $this->worker = $worker;
    }

    /**
     * Execute the console command.
     *
     * @return void
     */
    public function handle()
    {
        // 如果当前是维护模式，
        // 而且有 once 标志，
        // 就一直 sleep，不执行其他 job
        if ($this->downForMaintenance() && $this->option('once')) {
            return $this->worker->sleep($this->option('sleep'));
        }

        // 配置正在处理，已处理，处理失败的事件
        $this->listenForEvents();

        // 获取连接，如果命令没有指定，则用 config/queue.php 中 default 的值
        $connection = $this->argument('connection')
                        ?: $this->laravel['config']['queue.default'];

        // 获取队列名，默认为 default
        $queue = $this->getQueue($connection);

        // 执行队列 worker
        $this->runWorker(
            $connection, $queue
        );
    }

    /**
     * 执行 worker 实例
     * @param  string  $connection
     * @param  string  $queue
     * @return array
     */
    protected function runWorker($connection, $queue)
    {
        // 设置队列 worker 使用的 cache
        $this->worker->setCache($this->laravel['cache']->driver());

        // 处理队列的任务，
        // 如果设置了 once 标志，
        // 则只处理一次队列中的任务
        return $this->worker->{$this->option('once') ? 'runNextJob' : 'daemon'}(
            $connection, $queue, $this->gatherWorkerOptions()
        );
    }

    ...
}
```

## Illuminate\Queue\Worker

Worker 主要实现了队列守护进程的处理。

```php
namespace Illuminate\Queue;

use Exception;
...
use Illuminate\Contracts\Cache\Repository as CacheContract;

class Worker
{
    ...
    
    public function daemon($connectionName, $queue, WorkerOptions $options)
    {
        // 如果支持 pcntl，
        // 则监听系统信号并作出相应处理，
        // 监听了 SIGTERM、SIGUSR2、SIGCONT 这三个信号
        if ($this->supportsAsyncSignals()) {
            $this->listenForSignals();
        }

        // 从 cache 获取最后一个队列重新启动的时间戳
        $lastRestart = $this->getTimestampOfLastQueueRestart();

        while (true) {
            // 每次迭代检查一次守护进程是否该暂停
            if (!$this->daemonShouldRun($options, $connectionName, $queue)) {
                // 停止 worker 进程
                // sleep 或者退出 worker 进程
                $this->pauseWorker($options, $lastRestart);

                continue;
            }

            // 从队列中取出一个 job
            $job = $this->getNextJob(
                $this->manager->connection($connectionName), $queue
            );

            // 如果支持 pcntl 异步信号
            if ($this->supportsAsyncSignals()) {
                // 注册超时处理程序，
                // 如果一个 job 执行的时间太长就会触发这个处理，
                // 这个 job 就会被杀掉
                $this->registerTimeoutHandler($job, $options);
            }

            if ($job) {
                // 如果 job 不为空，则执行
                $this->runJob($job, $connectionName, $options);
            } else {
                // 如果 job 为空，则 sleep
                $this->sleep($options->sleep);
            }

            // 检查是否超出内存限制，
            // 或者有其他的信号指示，
            // 则重启队列 worker
            $this->stopIfNecessary($options, $lastRestart);
        }
    }
    
    /**
     * 暂停当前迭代的 worker
     */
    protected function pauseWorker(WorkerOptions $options, $lastRestart)
    {
        // sleep 
        $this->sleep($options->sleep > 0 ? $options->sleep : 1);

        // 检查是否超出内存限制，
        // 或者有其他的信号指示，
        // 则重启队列 worker
        $this->stopIfNecessary($options, $lastRestart);
    }
    
    protected function stopIfNecessary(WorkerOptions $options, $lastRestart)
    {
        // 如果退出标记为真
        if ($this->shouldQuit) {
            // 杀掉当前 worker 进程
            $this->kill();
        }

        // 如果占用内存超出设定
        if ($this->memoryExceeded($options->memory)) {
            // 退出当前脚本程序
            $this->stop(12);
        } elseif ($this->queueShouldRestart($lastRestart)) {
            $this->stop();
        }
    }
    
    ...
}
```

## 分发 Job

```php
<?php

namespace Illuminate\Foundation\Bus;

use Illuminate\Contracts\Bus\Dispatcher;

trait Dispatchable
{
    /**
     * 分发 job
     */
    public static function dispatch()
    {
        return new PendingDispatch(new static(...func_get_args()));
    }

    ...
}

```

```php
<?php

namespace Illuminate\Foundation\Bus;

use Illuminate\Contracts\Bus\Dispatcher;

class PendingDispatch
{
    ...
    
    /**
     * 设置延时时间
     */
    public function delay($delay)
    {
        $this->job->delay($delay);

        return $this;
    }

    public function __destruct()
    {
        // 在析构的时候真正地把 job 分发出去
        app(Dispatcher::class)->dispatch($this->job);
    }
    
    ...
}

```

```php
namespace Illuminate\Bus;

use Closure;
use RuntimeException;
use Illuminate\Pipeline\Pipeline;
use Illuminate\Contracts\Queue\Queue;
use Illuminate\Contracts\Queue\ShouldQueue;
use Illuminate\Contracts\Container\Container;
use Illuminate\Contracts\Bus\QueueingDispatcher;

class Dispatcher implements QueueingDispatcher
{
    ...

    // 把 command 分发给相应的处理程序
    // command 可以是一个 job
    public function dispatch($command)
    {
        // 如果 command 是 Illuminate\Contracts\Queue\ShouldQueue 的实例，
        // 且队列解析器不为空，
        // 则分发到队列
        if ($this->queueResolver && $this->commandShouldBeQueued($command)) {
            return $this->dispatchToQueue($command);
        }

        return $this->dispatchNow($command);
    }

    // 把 job 分发进队列
    public function dispatchToQueue($command)
    {
        // 取出 job 分发到的队列的连接
        $connection = $command->connection ?? null;

        // 获取队列
        $queue = call_user_func($this->queueResolver, $connection);

        if (! $queue instanceof Queue) {
            throw new RuntimeException('Queue resolver did not return a Queue implementation.');
        }

        if (method_exists($command, 'queue')) {
            return $command->queue($queue, $command);
        }

        // 把 job 放进队列
        return $this->pushCommandToQueue($queue, $command);
    }
    
    protected function pushCommandToQueue($queue, $command)
    {
        // 如果 job 有设置过 queue 和 delay 属性
        if (isset($command->queue, $command->delay)) {
            return $queue->laterOn($command->queue, $command->delay, $command);
        }

        // 如果 job 有设置过 queue 属性
        if (isset($command->queue)) {
            return $queue->pushOn($command->queue, $command);
        }

        // 如果 job 有设置过 delay 属性
        if (isset($command->delay)) {
            return $queue->later($command->delay, $command);
        }

        // 把 job 放进队列
        return $queue->push($command);
    }
    
    ...
}
```

Laravel 目前支持以下这些队列:
![](https://i.loli.net/2018/06/24/5b2faf9d54f07.jpeg)

我们看一下 Illuminate\Queue\DatabaseQueue，其他的实现思路都是差不多的

```php
<?php

namespace Illuminate\Queue;

class DatabaseQueue extends Queue implements QueueContract
{
    ...
    
    /**
     * 把 job 放进队列
     */
    public function push($job, $data = '', $queue = null)
    {
        return $this->pushToDatabase($queue, $this->createPayload($job, $data));
    }
    
    /**
     * 把 payload 插入数据库
     */
    protected function pushToDatabase($queue, $payload, $delay = 0, $attempts = 0)
    {
        return $this->database->table($this->table)->insertGetId($this->buildDatabaseRecord(
            $this->getQueue($queue), $payload, $this->availableAt($delay), $attempts
        ));
    }
    
    /**
     * 数据库中每行数据的字段如下
     */
    protected function buildDatabaseRecord($queue, $payload, $availableAt, $attempts = 0)
    {
        return [
            'queue' => $queue,
            'attempts' => $attempts,
            'reserved_at' => null,
            'available_at' => $availableAt,
            'created_at' => $this->currentTime(),
            'payload' => $payload,
        ];
    }
    
    ...
}

```

```php
<?php

namespace Illuminate\Queue;

use DateTimeInterface;
use Illuminate\Container\Container;
use Illuminate\Support\InteractsWithTime;

abstract class Queue
{
    ...
    
    /**
     * 生成 payload json 字符串内容
     */
    protected function createPayload($job, $data = '')
    {
        $payload = json_encode($this->createPayloadArray($job, $data));

        if (JSON_ERROR_NONE !== json_last_error()) {
            throw new InvalidPayloadException(
                'Unable to JSON encode payload. Error code: '.json_last_error()
            );
        }

        return $payload;
    }
    
    /**
     * 生成 payload 内容，数组格式
     */
    protected function createPayloadArray($job, $data = '')
    {
        return is_object($job)
                    ? $this->createObjectPayload($job)
                    : $this->createStringPayload($job, $data);
    }
    
    /**
     * payload 的数据格式如下
     */
    protected function createObjectPayload($job)
    {
        return [
            'displayName' => $this->getDisplayName($job),
            'job' => 'Illuminate\Queue\CallQueuedHandler@call',
            'maxTries' => $job->tries ?? null,
            'timeout' => $job->timeout ?? null,
            'timeoutAt' => $this->getJobExpiration($job),
            'data' => [
                'commandName' => get_class($job),
                'command' => serialize(clone $job),
            ],
        ];
    }
    
    ...
}
```

## 从队列获取 Job

以 DatabaseQueue 为例子，其他队列思路是一样的

```php
<?php

namespace Illuminate\Queue;

use Illuminate\Support\Carbon;
use Illuminate\Database\Connection;
use Illuminate\Queue\Jobs\DatabaseJob;
use Illuminate\Queue\Jobs\DatabaseJobRecord;
use Illuminate\Contracts\Queue\Queue as QueueContract;

class DatabaseQueue extends Queue implements QueueContract
{
    ...
    
    /**
     * 从队列中获取 job
     */
    public function pop($queue = null)
    {
        // 获取队列
        $queue = $this->getQueue($queue);

        return $this->database->transaction(function () use ($queue) {
            // 从数据库读取 job 数据
            if ($job = $this->getNextAvailableJob($queue)) {
                // 用拿到的 job 生成一个 DatabaseJob 实例
                return $this->marshalJob($queue, $job);
            }

            return null;
        });
    }
    
    protected function getNextAvailableJob($queue)
    {
        // 把 reserved_at 为 null 且 available_at 小于或等于当前时间，
        // 或者 reserved_at 小于等于当前时间，
        // 的数据取第一条出来
        $job = $this->database->table($this->table)
                    ->lockForUpdate()
                    ->where('queue', $this->getQueue($queue))
                    ->where(function ($query) {
                        $this->isAvailable($query);
                        $this->isReservedButExpired($query);
                    })
                    ->orderBy('id', 'asc')
                    ->first();

        return $job ? new DatabaseJobRecord((object) $job) : null;
    }
    
    protected function marshalJob($queue, $job)
    {
        // 更新数据库中的 job 对应数据的 reserved_at 和 attempts
        $job = $this->markJobAsReserved($job);

        // 创建 DatabaseJob 实例
        return new DatabaseJob(
            $this->container, $this, $job, $this->connectionName, $queue
        );
    }
    
    ...
}
```



