#pragma warning(disable: 4103)

#include "SocketComm.h"
#include "DBKFunc.h"
#include "DBKDrvr.h"
#include "IOPLDispatcher.h"
#include <ntddk.h>
#include <wsk.h>

// 全局Socket上下文
SOCKET_COMM_CONTEXT g_SocketContext = { 0 };

// WSK客户端调度表
const WSK_CLIENT_DISPATCH WskAppDispatch = {
    MAKE_WSK_VERSION(1, 0),
    0,
    NULL
};

// WSK监听Socket调度表
const WSK_CLIENT_LISTEN_DISPATCH WskListenDispatch = {
    SocketComm_AcceptEvent,
    NULL,  // WskInspectEvent
    NULL   // WskAbortEvent
};

// WSK连接Socket调度表（用于已接受的客户端连接）
const WSK_CLIENT_CONNECTION_DISPATCH WskConnectionDispatch = {
    NULL,  // WskReceiveEvent
    NULL,  // WskDisconnectEvent
    NULL   // WskSendBacklogEvent
};

//==============================================================================
// 初始化Socket通信
//==============================================================================
NTSTATUS SocketComm_Initialize(VOID)
{
    NTSTATUS status;
    WSK_CLIENT_NPI wskClientNpi;

    DbgPrint("[SocketComm] Initializing socket communication...\n");

    RtlZeroMemory(&g_SocketContext, sizeof(SOCKET_COMM_CONTEXT));
    KeInitializeSpinLock(&g_SocketContext.SpinLock);
    KeInitializeEvent(&g_SocketContext.SocketReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&g_SocketContext.ClientConnectedEvent, NotificationEvent, FALSE);

    // 注册WSK客户端
    wskClientNpi.ClientContext = &g_SocketContext;
    wskClientNpi.Dispatch = &WskAppDispatch;

    status = WskRegister(&wskClientNpi, &g_SocketContext.WskRegistration);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[SocketComm] WskRegister failed: 0x%X\n", status);
        return status;
    }

    // 捕获WSK提供程序NPI
    status = WskCaptureProviderNPI(&g_SocketContext.WskRegistration,
                                    WSK_INFINITE_WAIT,
                                    &g_SocketContext.WskProviderNpi);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[SocketComm] WskCaptureProviderNPI failed: 0x%X\n", status);
        WskDeregister(&g_SocketContext.WskRegistration);
        return status;
    }

    g_SocketContext.IsInitialized = TRUE;
    DbgPrint("[SocketComm] Socket communication initialized successfully\n");

    return STATUS_SUCCESS;
}

//==============================================================================
// 清理Socket通信
//==============================================================================
VOID SocketComm_Cleanup(VOID)
{
    DbgPrint("[SocketComm] Cleaning up socket communication...\n");

    SocketComm_StopListening();

    if (g_SocketContext.IsInitialized) {
        WskReleaseProviderNPI(&g_SocketContext.WskRegistration);
        WskDeregister(&g_SocketContext.WskRegistration);
        g_SocketContext.IsInitialized = FALSE;
    }

    DbgPrint("[SocketComm] Socket communication cleaned up\n");
}

//==============================================================================
// 创建监听Socket
//==============================================================================
NTSTATUS SocketComm_CreateListenSocket(VOID)
{
    NTSTATUS status;
    SOCKADDR_IN localAddress;
    PIRP irp;
    KEVENT completionEvent;

    DbgPrint("[SocketComm] Creating listen socket on port %d...\n", SOCKET_SERVER_PORT);

    // 准备本地地址
    RtlZeroMemory(&localAddress, sizeof(localAddress));
    localAddress.sin_family = AF_INET;
    localAddress.sin_addr.s_addr = INADDR_ANY;
    localAddress.sin_port = RtlUshortByteSwap(SOCKET_SERVER_PORT);

    // 创建IRP用于异步操作
    KeInitializeEvent(&completionEvent, NotificationEvent, FALSE);
    irp = IoAllocateIrp(1, FALSE);
    if (!irp) {
        DbgPrint("[SocketComm] Failed to allocate IRP\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoSetCompletionRoutine(irp, 
        (PIO_COMPLETION_ROUTINE)KeSetEvent,
        &completionEvent,
        TRUE, TRUE, TRUE);

    // 创建监听Socket
    status = g_SocketContext.WskProviderNpi.Dispatch->WskSocket(
        g_SocketContext.WskProviderNpi.Client,
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP,
        WSK_FLAG_LISTEN_SOCKET,
        &g_SocketContext,
        &WskListenDispatch,
        NULL,
        NULL,
        NULL,
        irp);

    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
        status = irp->IoStatus.Status;
    }

    if (!NT_SUCCESS(status)) {
        DbgPrint("[SocketComm] WskSocket failed: 0x%X\n", status);
        IoFreeIrp(irp);
        return status;
    }

    g_SocketContext.ListenSocket = (PWSK_SOCKET)irp->IoStatus.Information;

    // 绑定到本地地址
    KeResetEvent(&completionEvent);
    status = ((PWSK_PROVIDER_LISTEN_DISPATCH)g_SocketContext.ListenSocket->Dispatch)->WskBind(
        g_SocketContext.ListenSocket,
        (PSOCKADDR)&localAddress,
        0,
        irp);

    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
        status = irp->IoStatus.Status;
    }

    if (!NT_SUCCESS(status)) {
        DbgPrint("[SocketComm] WskBind failed: 0x%X\n", status);
        ((PWSK_PROVIDER_BASIC_DISPATCH)g_SocketContext.ListenSocket->Dispatch)->WskCloseSocket(
            g_SocketContext.ListenSocket, irp);
        IoFreeIrp(irp);
        return status;
    }

    IoFreeIrp(irp);
    DbgPrint("[SocketComm] Listen socket created successfully\n");
    return STATUS_SUCCESS;
}

//==============================================================================
// WSK接受连接事件回调
//==============================================================================
NTSTATUS WSKAPI SocketComm_AcceptEvent(
    PVOID SocketContext,
    ULONG Flags,
    PSOCKADDR LocalAddress,
    PSOCKADDR RemoteAddress,
    PWSK_SOCKET AcceptSocket,
    PVOID *AcceptSocketContext,
    CONST WSK_CLIENT_CONNECTION_DISPATCH **AcceptSocketDispatch)
{
    PSOCKADDR_IN remoteAddr = (PSOCKADDR_IN)RemoteAddress;
    
    DbgPrint("[SocketComm] Client connected from %d.%d.%d.%d:%d\n",
        remoteAddr->sin_addr.S_un.S_un_b.s_b1,
        remoteAddr->sin_addr.S_un.S_un_b.s_b2,
        remoteAddr->sin_addr.S_un.S_un_b.s_b3,
        remoteAddr->sin_addr.S_un.S_un_b.s_b4,
        RtlUshortByteSwap(remoteAddr->sin_port));

    // 如果已有客户端连接，拒绝新连接
    if (g_SocketContext.ClientSocket != NULL) {
        DbgPrint("[SocketComm] Rejecting connection - client already connected\n");
        return STATUS_REQUEST_NOT_ACCEPTED;
    }

    g_SocketContext.ClientSocket = AcceptSocket;
    *AcceptSocketContext = &g_SocketContext;
    *AcceptSocketDispatch = &WskConnectionDispatch;

    KeSetEvent(&g_SocketContext.ClientConnectedEvent, IO_NO_INCREMENT, FALSE);

    return STATUS_SUCCESS;
}

//==============================================================================
// 工作线程 - 处理Socket请求
//==============================================================================
VOID SocketComm_WorkerThread(PVOID Context)
{
    NTSTATUS status;
    PVOID receiveBuffer = NULL;
    PVOID sendBuffer = NULL;
    SOCKET_MESSAGE_HEADER msgHeader;
    SOCKET_RESPONSE_HEADER respHeader;
    ULONG bytesReceived;
    ULONG bytesReturned;
    PIRP irp;
    KEVENT completionEvent;
    WSK_BUF wskBuf;

    DbgPrint("[SocketComm] Worker thread started\n");

    // 分配缓冲区
    receiveBuffer = ExAllocatePool(NonPagedPool, SOCKET_BUFFER_SIZE);
    sendBuffer = ExAllocatePool(NonPagedPool, SOCKET_BUFFER_SIZE);

    if (!receiveBuffer || !sendBuffer) {
        DbgPrint("[SocketComm] Failed to allocate buffers\n");
        if (receiveBuffer) ExFreePool(receiveBuffer);
        if (sendBuffer) ExFreePool(sendBuffer);
        PsTerminateSystemThread(STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    KeInitializeEvent(&completionEvent, NotificationEvent, FALSE);

    while (!g_SocketContext.StopThread) {
        // 等待客户端连接
        status = KeWaitForSingleObject(&g_SocketContext.ClientConnectedEvent,
                                        Executive, KernelMode, FALSE, NULL);

        if (g_SocketContext.StopThread) break;

        DbgPrint("[SocketComm] Processing client connection...\n");

        while (g_SocketContext.ClientSocket && !g_SocketContext.StopThread) {
            // 接收消息头
            irp = IoAllocateIrp(1, FALSE);
            if (!irp) break;

            IoSetCompletionRoutine(irp, (PIO_COMPLETION_ROUTINE)KeSetEvent,
                                   &completionEvent, TRUE, TRUE, TRUE);

            wskBuf.Offset = 0;
            wskBuf.Length = sizeof(SOCKET_MESSAGE_HEADER);
            wskBuf.Mdl = IoAllocateMdl(&msgHeader, sizeof(SOCKET_MESSAGE_HEADER),
                                       FALSE, FALSE, NULL);
            if (!wskBuf.Mdl) {
                IoFreeIrp(irp);
                break;
            }

            MmBuildMdlForNonPagedPool(wskBuf.Mdl);

            status = ((PWSK_PROVIDER_CONNECTION_DISPATCH)g_SocketContext.ClientSocket->Dispatch)->WskReceive(
                g_SocketContext.ClientSocket,
                &wskBuf,
                0,
                irp);

            if (status == STATUS_PENDING) {
                KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
                status = irp->IoStatus.Status;
            }

            bytesReceived = (ULONG)irp->IoStatus.Information;
            IoFreeMdl(wskBuf.Mdl);
            IoFreeIrp(irp);

            if (!NT_SUCCESS(status) || bytesReceived != sizeof(SOCKET_MESSAGE_HEADER)) {
                DbgPrint("[SocketComm] Failed to receive header: 0x%X\n", status);
                break;
            }

            DbgPrint("[SocketComm] Received request - IOCTL: 0x%X, InputSize: %d\n",
                     msgHeader.IoControlCode, msgHeader.InputBufferSize);

            // 接收输入数据
            if (msgHeader.InputBufferSize > 0 && msgHeader.InputBufferSize <= SOCKET_BUFFER_SIZE) {
                irp = IoAllocateIrp(1, FALSE);
                if (!irp) break;

                KeResetEvent(&completionEvent);
                IoSetCompletionRoutine(irp, (PIO_COMPLETION_ROUTINE)KeSetEvent,
                                       &completionEvent, TRUE, TRUE, TRUE);

                wskBuf.Offset = 0;
                wskBuf.Length = msgHeader.InputBufferSize;
                wskBuf.Mdl = IoAllocateMdl(receiveBuffer, msgHeader.InputBufferSize,
                                           FALSE, FALSE, NULL);
                if (!wskBuf.Mdl) {
                    IoFreeIrp(irp);
                    break;
                }

                MmBuildMdlForNonPagedPool(wskBuf.Mdl);

                status = ((PWSK_PROVIDER_CONNECTION_DISPATCH)g_SocketContext.ClientSocket->Dispatch)->WskReceive(
                    g_SocketContext.ClientSocket,
                    &wskBuf,
                    0,
                    irp);

                if (status == STATUS_PENDING) {
                    KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
                    status = irp->IoStatus.Status;
                }

                IoFreeMdl(wskBuf.Mdl);
                IoFreeIrp(irp);

                if (!NT_SUCCESS(status)) {
                    DbgPrint("[SocketComm] Failed to receive data: 0x%X\n", status);
                    break;
                }
            }

            // 处理请求
            bytesReturned = 0;
            status = SocketComm_ProcessRequest(receiveBuffer, msgHeader.InputBufferSize,
                                               sendBuffer, msgHeader.OutputBufferSize,
                                               &bytesReturned, msgHeader.IoControlCode);

            // 发送响应头
            respHeader.Status = status;
            respHeader.DataSize = bytesReturned;
            respHeader.Reserved1 = 0;
            respHeader.Reserved2 = 0;

            irp = IoAllocateIrp(1, FALSE);
            if (!irp) break;

            KeResetEvent(&completionEvent);
            IoSetCompletionRoutine(irp, (PIO_COMPLETION_ROUTINE)KeSetEvent,
                                   &completionEvent, TRUE, TRUE, TRUE);

            wskBuf.Offset = 0;
            wskBuf.Length = sizeof(SOCKET_RESPONSE_HEADER);
            wskBuf.Mdl = IoAllocateMdl(&respHeader, sizeof(SOCKET_RESPONSE_HEADER),
                                       FALSE, FALSE, NULL);
            if (!wskBuf.Mdl) {
                IoFreeIrp(irp);
                break;
            }

            MmBuildMdlForNonPagedPool(wskBuf.Mdl);

            status = ((PWSK_PROVIDER_CONNECTION_DISPATCH)g_SocketContext.ClientSocket->Dispatch)->WskSend(
                g_SocketContext.ClientSocket,
                &wskBuf,
                0,
                irp);

            if (status == STATUS_PENDING) {
                KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
                status = irp->IoStatus.Status;
            }

            IoFreeMdl(wskBuf.Mdl);
            IoFreeIrp(irp);

            if (!NT_SUCCESS(status)) {
                DbgPrint("[SocketComm] Failed to send response header: 0x%X\n", status);
                break;
            }

            // 发送响应数据
            if (bytesReturned > 0) {
                irp = IoAllocateIrp(1, FALSE);
                if (!irp) break;

                KeResetEvent(&completionEvent);
                IoSetCompletionRoutine(irp, (PIO_COMPLETION_ROUTINE)KeSetEvent,
                                       &completionEvent, TRUE, TRUE, TRUE);

                wskBuf.Offset = 0;
                wskBuf.Length = bytesReturned;
                wskBuf.Mdl = IoAllocateMdl(sendBuffer, bytesReturned, FALSE, FALSE, NULL);
                if (!wskBuf.Mdl) {
                    IoFreeIrp(irp);
                    break;
                }

                MmBuildMdlForNonPagedPool(wskBuf.Mdl);

                status = ((PWSK_PROVIDER_CONNECTION_DISPATCH)g_SocketContext.ClientSocket->Dispatch)->WskSend(
                    g_SocketContext.ClientSocket,
                    &wskBuf,
                    0,
                    irp);

                if (status == STATUS_PENDING) {
                    KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
                    status = irp->IoStatus.Status;
                }

                IoFreeMdl(wskBuf.Mdl);
                IoFreeIrp(irp);

                if (!NT_SUCCESS(status)) {
                    DbgPrint("[SocketComm] Failed to send response data: 0x%X\n", status);
                    break;
                }
            }

            DbgPrint("[SocketComm] Request processed successfully\n");
        }

        // 客户端断开连接
        if (g_SocketContext.ClientSocket) {
            DbgPrint("[SocketComm] Client disconnected\n");
            g_SocketContext.ClientSocket = NULL;
            KeResetEvent(&g_SocketContext.ClientConnectedEvent);
        }
    }

    // 清理
    if (receiveBuffer) ExFreePool(receiveBuffer);
    if (sendBuffer) ExFreePool(sendBuffer);

    DbgPrint("[SocketComm] Worker thread terminated\n");
    PsTerminateSystemThread(STATUS_SUCCESS);
}

//==============================================================================
// 处理请求 - 调用原有的DispatchIoctl逻辑
//==============================================================================
NTSTATUS SocketComm_ProcessRequest(PVOID InputBuffer, ULONG InputSize,
                                    PVOID OutputBuffer, ULONG OutputSize,
                                    PULONG BytesReturned, ULONG IoControlCode)
{
    NTSTATUS status;
    IRP fakeIrp;
    PVOID systemBuffer;

    // 分配系统缓冲区
    systemBuffer = ExAllocatePool(NonPagedPool, max(InputSize, OutputSize));
    if (!systemBuffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // 复制输入数据
    if (InputSize > 0) {
        RtlCopyMemory(systemBuffer, InputBuffer, InputSize);
    }

    // 构造假IRP
    RtlZeroMemory(&fakeIrp, sizeof(IRP));
    fakeIrp.AssociatedIrp.SystemBuffer = systemBuffer;
    fakeIrp.Flags = IoControlCode;

    // 调用原有的处理函数
    status = DispatchIoctl(NULL, &fakeIrp);

    // 复制输出数据
    if (NT_SUCCESS(status) && OutputSize > 0) {
        ULONG copySize = min(OutputSize, InputSize);
        RtlCopyMemory(OutputBuffer, systemBuffer, copySize);
        *BytesReturned = copySize;
    } else {
        *BytesReturned = 0;
    }

    ExFreePool(systemBuffer);
    return status;
}

//==============================================================================
// 开始监听
//==============================================================================
NTSTATUS SocketComm_StartListening(VOID)
{
    NTSTATUS status;
    HANDLE threadHandle;
    OBJECT_ATTRIBUTES objAttr;

    if (!g_SocketContext.IsInitialized) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (g_SocketContext.IsListening) {
        return STATUS_SUCCESS;
    }

    DbgPrint("[SocketComm] Starting socket listener...\n");

    // 创建监听Socket
    status = SocketComm_CreateListenSocket();
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // 创建工作线程
    g_SocketContext.StopThread = FALSE;
    InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    status = PsCreateSystemThread(&threadHandle, THREAD_ALL_ACCESS, &objAttr,
                                   NULL, NULL, SocketComm_WorkerThread, NULL);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[SocketComm] Failed to create worker thread: 0x%X\n", status);
        return status;
    }

    // 获取线程对象
    status = ObReferenceObjectByHandle(threadHandle, THREAD_ALL_ACCESS, NULL,
                                        KernelMode, &g_SocketContext.WorkerThread, NULL);
    ZwClose(threadHandle);

    if (!NT_SUCCESS(status)) {
        DbgPrint("[SocketComm] Failed to reference thread object: 0x%X\n", status);
        return status;
    }

    g_SocketContext.IsListening = TRUE;
    DbgPrint("[SocketComm] Socket listener started on port %d\n", SOCKET_SERVER_PORT);

    return STATUS_SUCCESS;
}

//==============================================================================
// 停止监听
//==============================================================================
VOID SocketComm_StopListening(VOID)
{
    PIRP irp;
    KEVENT completionEvent;

    if (!g_SocketContext.IsListening) {
        return;
    }

    DbgPrint("[SocketComm] Stopping socket listener...\n");

    // 停止工作线程
    g_SocketContext.StopThread = TRUE;
    KeSetEvent(&g_SocketContext.ClientConnectedEvent, IO_NO_INCREMENT, FALSE);

    if (g_SocketContext.WorkerThread) {
        KeWaitForSingleObject(g_SocketContext.WorkerThread, Executive,
                              KernelMode, FALSE, NULL);
        ObDereferenceObject(g_SocketContext.WorkerThread);
        g_SocketContext.WorkerThread = NULL;
    }

    // 关闭客户端Socket
    if (g_SocketContext.ClientSocket) {
        irp = IoAllocateIrp(1, FALSE);
        if (irp) {
            KeInitializeEvent(&completionEvent, NotificationEvent, FALSE);
            IoSetCompletionRoutine(irp, (PIO_COMPLETION_ROUTINE)KeSetEvent,
                                   &completionEvent, TRUE, TRUE, TRUE);

            ((PWSK_PROVIDER_BASIC_DISPATCH)g_SocketContext.ClientSocket->Dispatch)->WskCloseSocket(
                g_SocketContext.ClientSocket, irp);

            KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
            IoFreeIrp(irp);
        }
        g_SocketContext.ClientSocket = NULL;
    }

    // 关闭监听Socket
    if (g_SocketContext.ListenSocket) {
        irp = IoAllocateIrp(1, FALSE);
        if (irp) {
            KeInitializeEvent(&completionEvent, NotificationEvent, FALSE);
            IoSetCompletionRoutine(irp, (PIO_COMPLETION_ROUTINE)KeSetEvent,
                                   &completionEvent, TRUE, TRUE, TRUE);

            ((PWSK_PROVIDER_BASIC_DISPATCH)g_SocketContext.ListenSocket->Dispatch)->WskCloseSocket(
                g_SocketContext.ListenSocket, irp);

            KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
            IoFreeIrp(irp);
        }
        g_SocketContext.ListenSocket = NULL;
    }

    g_SocketContext.IsListening = FALSE;
    DbgPrint("[SocketComm] Socket listener stopped\n");
}


