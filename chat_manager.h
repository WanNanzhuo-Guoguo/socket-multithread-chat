#ifndef CHAT_MANAGER_H
#define CHAT_MANAGER_H

#include "common.h"

/* 在接收客户端连接前调用一次；所有工作线程停止后再执行销毁 */
ChatStatus chat_manager_init(void);

/* 注册一个会话。注册成功后，注册表会持有该会话的一份引用 */
ChatStatus chat_register(ClientSession *session, const char *username);

/* 处理一条解码完成的消息。MSG_LOGIN 登录消息也在这里处理，
 * 无论上层是否显式调用 chat_register，都可以正常分发消息 */
ChatStatus chat_process(ClientSession *session, const ChatMessage *msg);

/* 将会话从注册表移除、广播下线通知，并等待直到仅剩工作线程持有的引用。
 * 支持多次调用 */
void chat_unregister(ClientSession *session);

/* 对消息编码，借助会话发送锁和传输层帧协议发送一条消息 */
ChatStatus chat_send(ClientSession *session, const ChatMessage *msg);

/* ClientSession 生命周期配套的引用计数工具函数。
 * 服务端初始化会话时 refs=1，属于工作线程持有引用。
 * 最后一次 release 会关闭文件描述符、销毁发送锁并释放会话内存 */
void chat_session_retain(ClientSession *session);
void chat_session_release(ClientSession *session);

/* 必须在所有工作线程都完成会话注销与释放之后再调用 */
void chat_manager_destroy(void);

#endif
