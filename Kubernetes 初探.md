---
title: Kubernetes 初探
date: 2019-03-18 18:07:34
tags: kubernetes
---

Kubernetes 是 Google 开源的容器集群管理系统，构建在 Docker 技术之上，为容器化的应用提供资源调度、部署运行、服务发现、扩容缩容等整一套功能，它最大的优点是可以显著提升整个集群的总 CPU 利用率，所以成为众多容器调度框架中最受欢迎的调度模式。 
在容器技术日益火热的今天，Kubernetes 渐渐成为了后台开发人员一门必须掌握的技能。本文主要介绍一些基本概念，以及体验一下整体流程。

<!--more-->

# Kubernetes 能做什么

- 服务发现
- 负载均衡
- 自动更新和回滚
- 管理外部存储
- 横向自动扩缩容
- 应用健康检查
- 增减应用实例副本
- 资源监测
- 日志采集和存储
- 认证和鉴权

# 为什么要用 Kubernetes

![](why_containers.svg)

## 传统部署方式
传统部署方式，在部署软件时会利用系统的包管理工具来安装。这种方式有个很大的缺点，会让系统上应用的执行、配置、依赖库和生命周期等变得混乱。传统的方式也可以用虚拟机来打包应用，但虚拟机太重了。

## 容器部署方式
容器是在操作系统层面的虚拟化。容器相互之间，和宿主机都是隔离的，每个容器有自己的文件系统，一个容器无法访问其他容器的内容，而且容器所使用的系统资源是可以限定的。容器相对虚拟机更容易构建，而且由于容器和底层的硬件基础是解耦的，所以很容易垮平台，垮系统。
容器方式的优点:
1. 灵活的应用创建和部署
2. 持续集成和部署
3. 开发和部署分离
4. 便于监控
5. 开发、测试和生产环境一致
6. 垮平台
7. 以程序为中心的管理
8. 松耦合，可扩展，有利于微服务管理
9. 资源隔离
10. 资源利用率高


# 简单体验

## 创建集群

### 集群架构

![](module_01_cluster.svg)

- master: 管理整个集群，如调度、维护程序状态、弹性扩缩容、应用升级、回滚
- node: 工作节点，通过 kubelete 管理节点以及和 master 节点通信

### 安装

[k8s-for-docker-desktop](https://github.com/AliyunContainerService/k8s-for-docker-desktop)

### 运行

```bash
kubectl version
kubectl cluster-info
kubectl cluster-info dump
kubectl get nodes
```

## 部署应用

![](module_02_first_app.svg)

```bash
kubectl run k8s-bootcamp --image=k8s-bootcamp:v1.0 --port=8080
kubectl get deployments

kubectl proxy
curl http://localhost:8001/version

export POD_NAME=$(kubectl get pods -o go-template --template '{{range .items}}{{.metadata.name}}{{"\n"}}{{end}}')
echo Name of the Pod: $POD_NAME

curl http://localhost:8001/api/v1/namespaces/default/pods/$POD_NAME/proxy/
```

## 探索应用程序

### Pods 和 Nodes

#### Pods

![](module_03_pods.svg)

在Kubernetes集群中，Pod是所有业务类型的基础，它是一个或多个容器的组合。这些容器共享存储、网络和命名空间，以及如何运行的规范。在Pod中，所有容器都被同一安排和调度，并运行在共享的上下文中。对于具体应用而言，Pod是它们的逻辑主机，Pod包含业务相关的多个应用容器。

#### Nodes

![](module_03_nodes.svg)

Node 是 Kubernetes 的工作节点，以前叫做 minion。Node 可以是一个虚拟机或者物理机器。每个 Node 都有用于运行 pods 的必要服务，并由 master 组件管理。Node 上的服务包括 Docker、kubelet 和 kube-proxy。


### 探索一下

```bash
kubectl get pods
kubectl describe pods

kubectl proxy
curl http://localhost:8001/version
export POD_NAME=$(kubectl get pods -o go-template --template '{{range .items}}{{.metadata.name}}{{"\n"}}{{end}}')
echo Name of the Pod: $POD_NAME
curl http://localhost:8001/api/v1/namespaces/default/pods/$POD_NAME/proxy/

kubectl logs $POD_NAME
kubectl exec $POD_NAME env
kubectl exec -ti $POD_NAME bash
cat server.js
curl localhost:8080
```

## 利用服务开放应用程序

### Service

![](module_04_services.svg)

Kubernetes Service 定义了这样一种抽象：逻辑上的一组 Pod，一种可以访问它们的策略。

开放服务的方式：
- ClusterIP
- NodePort
- LoadBalancer
- ExternalName

### Label

![](module_04_labels.svg)

Label 以 key/value 键值对的形式附加到任何对象上，如 Pod，Service，Node 等。
主要用来组织和分类对象。

### 开放应用程序

```bash
# Create a new service
kubectl get pods
kubectl get services
kubectl expose deployment/k8s-bootcamp --type="NodePort" --port 8080
kubectl describe services/k8s-bootcamp

export NODE_PORT=$(kubectl get services/k8s-bootcamp -o go-template='{{(index .spec.ports 0).nodePort}}')
echo NODE_PORT=$NODE_PORT

curl localhost:$NODE_PORT

# Using labels
kubectl describe deployment
kubectl get pods -l run=k8s-bootcamp
kubectl get services -l run=k8s-bootcamp
kubectl label pod $POD_NAME app=v1
kubectl describe pods $POD_NAME
kubectl get pods -l app=v1

# Deleting a service
kubectl delete service -l run=k8s-bootcamp
kubectl get services
curl localhost:$NODE_PORT

kubectl exec -ti $POD_NAME curl localhost:8080
```

## 应用程序扩容

![](module_05_scaling1.svg) 
![](module_05_scaling2.svg)

```bash
# Scaling a deployment
kubectl get deployments
kubectl scale deployments/k8s-bootcamp --replicas=4
kubectl get deployments
kubectl get pods -o wide
kubectl describe deployments/k8s-bootcamp

# Load Balancing
kubectl describe deployments/k8s-bootcamp

export NODE_PORT=$(kubectl get services/k8s-bootcamp -o go-template='{{(index .spec.ports 0).nodePort}}')
echo NODE_PORT=$NODE_PORT

curl localhost:$NODE_PORT

# Scale Down
kubectl scale deployments/k8s-bootcamp --replicas=2
kubectl get deployments
kubectl get pods -o wide
```

## 应用程序升级

![](module_06_rollingupdates1.svg) 
![](module_06_rollingupdates2.svg) 
![](module_06_rollingupdates3.svg) 
![](module_06_rollingupdates4.svg) 

```bash
# Update the version of the app
kubectl get deployments
kubectl get pods
kubectl describe pods
kubectl set image deployments/k8s-bootcamp k8s-bootcamp=k8s-bootcamp:v2.0
kubectl get pods

# Verify an update
kubectl describe services/k8s-bootcamp

export NODE_PORT=$(kubectl get services/k8s-bootcamp -o go-template='{{(index .spec.ports 0).nodePort}}')
echo NODE_PORT=$NODE_PORT

curl localhost:$NODE_PORT

kubectl rollout status deployments/k8s-bootcamp
kubectl describe pods

# Rollback an update
kubectl set image deployments/k8s-bootcamp k8s-bootcamp=gcr.io/google-samples/kubernetes-bootcamp:v10
kubectl get deployments
kubectl get pods
kubectl describe pods
kubectl rollout undo deployments/k8s-bootcamp
kubectl get pods
kubectl describe pods
```


# 参考

[1. What is Kubernetes](https://kubernetes.io/docs/concepts/overview/what-is-kubernetes/)
[2. Kubernetes Tutorials](https://kubernetes.io/docs/tutorials/)