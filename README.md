# Socket 多线程聊天系统

基于 Linux TCP Socket、POSIX pthread、cJSON 的多客户端聊天程序。

## 功能
- 每个连接由独立工作线程处理
- 用户登录、群聊、私聊、在线列表与退出
- 4 字节网络字节序长度前缀 + UTF-8 JSON 消息
- 在线会话管理与互斥锁保护

## 环境
Ubuntu Linux，GCC，pthread，cJSON 开发库。

```bash
sudo apt update
sudo apt install build-essential libcjson-dev
make
```

## 运行
在第一个终端：
```bash
./server
```
在其他终端：
```bash
./client
```

服务器目前仅绑定 `127.0.0.1:8888`，默认仅支持本机客户端连接。

## 项目结构
- `server.c`：Socket 监听、accept 与 pthread 创建
- `handler.c/.h`：客户端工作线程
- `chat_manager.c/.h`：登录、群聊、私聊、在线用户与会话管理
- `transport.c/.h`：TCP 帧发送与接收
- `protocol.c/.h`：JSON 编码与解码
- `common.h`：共享常量、结构体和状态定义
- `client.c`：命令行客户端
- `tests/`：测试源文件
- `basic_demo/`：基础 Socket 示例

## 说明
源代码来自课程项目，清理时保留核心实现及测试源文件；请在 Ubuntu 上执行 `make` 和多客户端功能测试后再发布正式版本。
