#ifndef SOCKETCOMM_H
#define SOCKETCOMM_H

#include <ntddk.h>
#include <wsk.h>

// Socket通信配置
#define SOCKET_SERVER_PORT 28996  // 监听端口
#define SOCKET_BUFFER_SIZE 65536  // 缓冲区大小

// Socket通信状态
typedef struct _SOCKET_COMM_CONTEXT {
    PWSK_SOCKET ListenSocket;
    PWSK_SOCKET ClientSocket;
    WSK_REGISTRATION WskRegistration;
    WSK_PROVIDER_NPI WskProviderNpi;
    WSK_CLIENT_DISPATCH WskClientDispatch;
    KEVENT SocketReadyEvent;
    KEVENT ClientConnectedEvent;
    BOOLEAN IsInitialized;
    BOOLEAN IsListening;
    PVOID WorkerThread;
    BOOLEAN StopThread;
    KSPIN_LOCK SpinLock;
} SOCKET_COMM_CONTEXT, *PSOCKET_COMM_CONTEXT;

// 消息头结构
typedef struct _SOCKET_MESSAGE_HEADER {
    ULONG IoControlCode;      // IOCTL代码
    ULONG InputBufferSize;    // 输入缓冲区大小
    ULONG OutputBufferSize;   // 输出缓冲区大小
    ULONG Reserved;           // 保留字段
} SOCKET_MESSAGE_HEADER, *PSOCKET_MESSAGE_HEADER;

// 响应头结构
typedef struct _SOCKET_RESPONSE_HEADER {
    NTSTATUS Status;          // 操作状态
    ULONG DataSize;           // 返回数据大小
    ULONG Reserved1;
    ULONG Reserved2;
} SOCKET_RESPONSE_HEADER, *PSOCKET_RESPONSE_HEADER;

// 函数声明
NTSTATUS SocketComm_Initialize(VOID);
VOID SocketComm_Cleanup(VOID);
NTSTATUS SocketComm_StartListening(VOID);
VOID SocketComm_StopListening(VOID);

// 内部函数
NTSTATUS SocketComm_CreateListenSocket(VOID);
VOID SocketComm_WorkerThread(PVOID Context);
NTSTATUS SocketComm_ProcessRequest(PVOID InputBuffer, ULONG InputSize, 
                                    PVOID OutputBuffer, ULONG OutputSize,
                                    PULONG BytesReturned, ULONG IoControlCode);

// WSK回调函数
NTSTATUS WSKAPI SocketComm_AcceptEvent(
    PVOID SocketContext,
    ULONG Flags,
    PSOCKADDR LocalAddress,
    PSOCKADDR RemoteAddress,
    PWSK_SOCKET AcceptSocket,
    PVOID *AcceptSocketContext,
    CONST WSK_CLIENT_CONNECTION_DISPATCH **AcceptSocketDispatch
);

// 全局上下文
extern SOCKET_COMM_CONTEXT g_SocketContext;

#endif // SOCKETCOMM_H


