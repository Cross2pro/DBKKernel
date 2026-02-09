/*
 * DBK驱动Socket通信客户端 (C++版本)
 * 用于通过Socket与内核驱动进行通信，替代传统的IRP/DeviceIoControl方式
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")

// Socket配置
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 28996

// IOCTL代码定义
#define IOCTL_CE_READMEMORY         0x9C402000
#define IOCTL_CE_WRITEMEMORY        0x9C402004
#define IOCTL_CE_OPENPROCESS        0x9C402008
#define IOCTL_CE_GETVERSION         0x9C4020C0
#define IOCTL_CE_GETPEPROCESS       0x9C402034
#define IOCTL_CE_READPHYSICALMEMORY 0x9C402038

// 消息头结构
#pragma pack(push, 1)
typedef struct _SOCKET_MESSAGE_HEADER {
    ULONG IoControlCode;
    ULONG InputBufferSize;
    ULONG OutputBufferSize;
    ULONG Reserved;
} SOCKET_MESSAGE_HEADER, *PSOCKET_MESSAGE_HEADER;

typedef struct _SOCKET_RESPONSE_HEADER {
    LONG Status;
    ULONG DataSize;
    ULONG Reserved1;
    ULONG Reserved2;
} SOCKET_RESPONSE_HEADER, *PSOCKET_RESPONSE_HEADER;
#pragma pack(pop)

class DBKSocketClient {
private:
    SOCKET sock;
    bool connected;

    bool RecvExact(void* buffer, size_t size) {
        char* ptr = (char*)buffer;
        size_t received = 0;
        
        while (received < size) {
            int ret = recv(sock, ptr + received, (int)(size - received), 0);
            if (ret <= 0) {
                printf("[-] 接收数据失败: %d\n", WSAGetLastError());
                return false;
            }
            received += ret;
        }
        return true;
    }

public:
    DBKSocketClient() : sock(INVALID_SOCKET), connected(false) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~DBKSocketClient() {
        Disconnect();
        WSACleanup();
    }

    bool Connect(const char* host = SERVER_HOST, int port = SERVER_PORT) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            printf("[-] 创建Socket失败: %d\n", WSAGetLastError());
            return false;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        inet_pton(AF_INET, host, &serverAddr.sin_addr);

        if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            printf("[-] 连接失败: %d\n", WSAGetLastError());
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }

        connected = true;
        printf("[+] 成功连接到驱动 %s:%d\n", host, port);
        return true;
    }

    void Disconnect() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            connected = false;
            printf("[+] 已断开连接\n");
        }
    }

    bool SendRequest(ULONG ioctlCode, void* inputData, ULONG inputSize, 
                     void* outputData, ULONG outputSize, LONG* status) {
        if (!connected) {
            printf("[-] 未连接到服务器\n");
            return false;
        }

        // 发送消息头
        SOCKET_MESSAGE_HEADER msgHeader;
        msgHeader.IoControlCode = ioctlCode;
        msgHeader.InputBufferSize = inputSize;
        msgHeader.OutputBufferSize = outputSize;
        msgHeader.Reserved = 0;

        if (send(sock, (char*)&msgHeader, sizeof(msgHeader), 0) == SOCKET_ERROR) {
            printf("[-] 发送消息头失败: %d\n", WSAGetLastError());
            return false;
        }

        // 发送输入数据
        if (inputSize > 0 && inputData) {
            if (send(sock, (char*)inputData, inputSize, 0) == SOCKET_ERROR) {
                printf("[-] 发送输入数据失败: %d\n", WSAGetLastError());
                return false;
            }
        }

        // 接收响应头
        SOCKET_RESPONSE_HEADER respHeader;
        if (!RecvExact(&respHeader, sizeof(respHeader))) {
            return false;
        }

        if (status) {
            *status = respHeader.Status;
        }

        // 接收响应数据
        if (respHeader.DataSize > 0 && outputData) {
            if (!RecvExact(outputData, min(respHeader.DataSize, outputSize))) {
                return false;
            }
        }

        return true;
    }

    bool GetVersion(ULONG* version) {
        LONG status;
        if (!SendRequest(IOCTL_CE_GETVERSION, NULL, 0, version, sizeof(ULONG), &status)) {
            return false;
        }

        if (status == 0) {
            printf("[+] 驱动版本: %u\n", *version);
            return true;
        } else {
            printf("[-] 获取版本失败，状态码: 0x%08X\n", status);
            return false;
        }
    }

    bool OpenProcess(DWORD processId, UINT64* handle) {
        struct {
            UINT64 h;
            BYTE special;
        } output;

        LONG status;
        if (!SendRequest(IOCTL_CE_OPENPROCESS, &processId, sizeof(DWORD), 
                        &output, sizeof(output), &status)) {
            return false;
        }

        if (status == 0) {
            *handle = output.h;
            printf("[+] 打开进程 PID=%u 成功\n", processId);
            printf("    句柄: 0x%016llX\n", output.h);
            printf("    特殊标志: %d\n", output.special);
            return true;
        } else {
            printf("[-] 打开进程失败，状态码: 0x%08X\n", status);
            return false;
        }
    }

    bool ReadProcessMemory(UINT64 processId, UINT64 address, WORD size, void* buffer) {
        struct {
            UINT64 processid;
            UINT64 startaddress;
            WORD bytestoread;
        } input;

        input.processid = processId;
        input.startaddress = address;
        input.bytestoread = size;

        BYTE* tempBuffer = new BYTE[size + sizeof(input)];
        LONG status;

        if (!SendRequest(IOCTL_CE_READMEMORY, &input, sizeof(input), 
                        tempBuffer, size + sizeof(input), &status)) {
            delete[] tempBuffer;
            return false;
        }

        if (status == 0) {
            memcpy(buffer, tempBuffer + sizeof(input), size);
            printf("[+] 读取内存成功: PID=%llu, 地址=0x%llX, 大小=%u\n", 
                   processId, address, size);
            delete[] tempBuffer;
            return true;
        } else {
            printf("[-] 读取内存失败，状态码: 0x%08X\n", status);
            delete[] tempBuffer;
            return false;
        }
    }

    bool GetPEProcess(DWORD processId, UINT64* peprocess) {
        LONG status;
        if (!SendRequest(IOCTL_CE_GETPEPROCESS, &processId, sizeof(DWORD), 
                        peprocess, sizeof(UINT64), &status)) {
            return false;
        }

        if (status == 0) {
            printf("[+] EPROCESS地址: 0x%016llX\n", *peprocess);
            return true;
        } else {
            printf("[-] 获取EPROCESS失败，状态码: 0x%08X\n", status);
            return false;
        }
    }
};

void PrintHexDump(const void* data, size_t size, UINT64 baseAddress = 0) {
    const BYTE* bytes = (const BYTE*)data;
    for (size_t i = 0; i < size; i += 16) {
        printf("  %08llX: ", baseAddress + i);
        
        // 打印十六进制
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", bytes[i + j]);
            } else {
                printf("   ");
            }
        }
        
        printf(" ");
        
        // 打印ASCII
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            BYTE b = bytes[i + j];
            printf("%c", (b >= 32 && b < 127) ? b : '.');
        }
        
        printf("\n");
    }
}

int main() {
    printf("============================================================\n");
    printf("DBK驱动Socket通信客户端测试 (C++版本)\n");
    printf("============================================================\n");

    DBKSocketClient client;

    // 连接到驱动
    if (!client.Connect()) {
        return 1;
    }

    // 测试1: 获取驱动版本
    printf("\n[测试1] 获取驱动版本\n");
    printf("------------------------------------------------------------\n");
    ULONG version;
    client.GetVersion(&version);

    // 测试2: 打开当前进程
    printf("\n[测试2] 打开当前进程\n");
    printf("------------------------------------------------------------\n");
    DWORD currentPid = GetCurrentProcessId();
    printf("当前进程PID: %u\n", currentPid);
    UINT64 handle;
    client.OpenProcess(currentPid, &handle);

    // 测试3: 获取EPROCESS
    printf("\n[测试3] 获取EPROCESS结构\n");
    printf("------------------------------------------------------------\n");
    UINT64 peprocess;
    client.GetPEProcess(currentPid, &peprocess);

    // 测试4: 读取内存
    printf("\n[测试4] 读取进程内存\n");
    printf("------------------------------------------------------------\n");
    UINT64 testAddress = 0x400000; // 典型的PE文件基址
    BYTE memory[64];
    if (client.ReadProcessMemory(currentPid, testAddress, sizeof(memory), memory)) {
        printf("内存数据:\n");
        PrintHexDump(memory, sizeof(memory), testAddress);
    }

    printf("\n============================================================\n");
    printf("测试完成！\n");
    printf("============================================================\n");

    client.Disconnect();
    
    printf("\n按任意键退出...");
    getchar();
    return 0;
}


