# DBK驱动 - 纯Socket通信完整指南

## 重要说明

**本驱动已完全移除IRP通信机制，仅使用Socket通信！**

- ❌ 不再创建设备对象 (Device Object)
- ❌ 不再创建符号链接 (Symbolic Link)
- ❌ 不再使用 DeviceIoControl API
- ❌ 不再处理 IRP_MJ_DEVICE_CONTROL
- ✅ 仅通过TCP Socket通信 (端口28996)

## 架构概述

```
┌─────────────────────────────────────────────────────────────┐
│                    用户态应用程序                              │
│         (Python / C++ / C# / Java / Go / ...)                │
│                                                               │
│  使用标准Socket API连接到 127.0.0.1:28996                     │
└────────────────────────┬────────────────────────────────────┘
                         │
                         │ TCP/IP Socket连接
                         │
                         ↓
┌─────────────────────────────────────────────────────────────┐
│              Windows网络栈 (TCP/IP)                           │
│  - 处理TCP连接                                                │
│  - 数据包传输                                                 │
│  - 本地回环优化                                               │
└────────────────────────┬────────────────────────────────────┘
                         │
                         │ 本地回环 (Loopback)
                         │
                         ↓
┌─────────────────────────────────────────────────────────────┐
│              DBK内核驱动 (Kernel Mode)                        │
│                                                               │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  WSK (Winsock Kernel) 监听Socket                     │    │
│  │  - 监听端口: 28996                                    │    │
│  │  - 绑定地址: 127.0.0.1 (仅本地)                      │    │
│  └─────────────────────┬───────────────────────────────┘    │
│                        │                                      │
│                        ↓                                      │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  SocketComm_WorkerThread (工作线程)                  │    │
│  │  1. 接收消息头 (16字节)                              │    │
│  │  2. 接收输入数据                                      │    │
│  │  3. 调用 DispatchIoctl 处理                          │    │
│  │  4. 发送响应头 (16字节)                              │    │
│  │  5. 发送输出数据                                      │    │
│  └─────────────────────┬───────────────────────────────┘    │
│                        │                                      │
│                        ↓                                      │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  DispatchIoctl (业务逻辑处理)                        │    │
│  │  - 读取进程内存                                       │    │
│  │  - 写入进程内存                                       │    │
│  │  - 打开进程/线程                                      │    │
│  │  - 获取系统信息                                       │    │
│  │  - ... 所有原有功能                                  │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

## 通信协议详解

### 1. 连接建立

```python
import socket

# 创建TCP Socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# 连接到驱动
sock.connect(("127.0.0.1", 28996))

# 连接成功！现在可以发送请求
```

### 2. 请求格式

每个请求由两部分组成：**消息头** + **输入数据**

#### 消息头结构 (16字节)

```c
typedef struct _SOCKET_MESSAGE_HEADER {
    ULONG IoControlCode;      // IOCTL代码 (4字节)
    ULONG InputBufferSize;    // 输入数据大小 (4字节)
    ULONG OutputBufferSize;   // 期望输出大小 (4字节)
    ULONG Reserved;           // 保留字段 (4字节)
} SOCKET_MESSAGE_HEADER;
```

**字段说明：**
- `IoControlCode`: 要执行的操作代码（如 0x9C4020C0 表示获取版本）
- `InputBufferSize`: 后续输入数据的字节数
- `OutputBufferSize`: 期望驱动返回的数据大小
- `Reserved`: 保留，必须为0

#### 输入数据

紧跟在消息头后面，大小由 `InputBufferSize` 指定。

**完整请求示例（Python）：**

```python
import struct

# 构造消息头
ioctl_code = 0x9C4020C0  # IOCTL_CE_GETVERSION
input_size = 0           # 无输入数据
output_size = 4          # 期望返回4字节
reserved = 0

header = struct.pack("<IIII", ioctl_code, input_size, output_size, reserved)

# 发送消息头
sock.sendall(header)

# 如果有输入数据，继续发送
# sock.sendall(input_data)
```

### 3. 响应格式

每个响应由两部分组成：**响应头** + **输出数据**

#### 响应头结构 (16字节)

```c
typedef struct _SOCKET_RESPONSE_HEADER {
    LONG Status;              // NTSTATUS状态码 (4字节)
    ULONG DataSize;           // 返回数据大小 (4字节)
    ULONG Reserved1;          // 保留字段 (4字节)
    ULONG Reserved2;          // 保留字段 (4字节)
} SOCKET_RESPONSE_HEADER;
```

**字段说明：**
- `Status`: 操作结果状态码（0表示成功，STATUS_SUCCESS）
- `DataSize`: 后续输出数据的字节数
- `Reserved1/2`: 保留，忽略

#### 输出数据

紧跟在响应头后面，大小由 `DataSize` 指定。

**完整响应接收示例（Python）：**

```python
# 接收响应头
resp_header = sock.recv(16)
status, data_size, _, _ = struct.unpack("<iIII", resp_header)

# 检查状态
if status != 0:
    print(f"操作失败，状态码: 0x{status:08X}")
    return None

# 接收输出数据
if data_size > 0:
    output_data = sock.recv(data_size)
    return output_data
```

## 完整通信示例

### 示例1: 获取驱动版本

```python
import socket
import struct

# 连接
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("127.0.0.1", 28996))

# 发送请求
header = struct.pack("<IIII", 0x9C4020C0, 0, 4, 0)
sock.sendall(header)

# 接收响应
resp_header = sock.recv(16)
status, data_size, _, _ = struct.unpack("<iIII", resp_header)

if status == 0 and data_size == 4:
    version_data = sock.recv(4)
    version = struct.unpack("<I", version_data)[0]
    print(f"驱动版本: {version}")

sock.close()
```

### 示例2: 打开进程

```python
import socket
import struct

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("127.0.0.1", 28996))

# 准备输入数据
process_id = 1234
input_data = struct.pack("<I", process_id)

# 发送请求
header = struct.pack("<IIII", 0x9C402008, 4, 9, 0)  # IOCTL_CE_OPENPROCESS
sock.sendall(header)
sock.sendall(input_data)

# 接收响应
resp_header = sock.recv(16)
status, data_size, _, _ = struct.unpack("<iIII", resp_header)

if status == 0:
    output_data = sock.recv(data_size)
    handle, special = struct.unpack("<QB", output_data)
    print(f"进程句柄: 0x{handle:016X}")

sock.close()
```

### 示例3: 读取进程内存

```python
import socket
import struct

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("127.0.0.1", 28996))

# 准备输入数据
process_id = 1234
address = 0x400000
size = 64

input_data = struct.pack("<QQH", process_id, address, size)

# 发送请求
header = struct.pack("<IIII", 0x9C402000, len(input_data), size + 18, 0)
sock.sendall(header)
sock.sendall(input_data)

# 接收响应
resp_header = sock.recv(16)
status, data_size, _, _ = struct.unpack("<iIII", resp_header)

if status == 0:
    output_data = sock.recv(data_size)
    # 跳过输入结构，获取实际内存数据
    memory = output_data[18:18+size]
    print(f"读取到 {len(memory)} 字节")

sock.close()
```

## 支持的IOCTL命令

所有原有的IOCTL命令都被完整保留：

| IOCTL代码 | 命令名称 | 功能描述 |
|-----------|---------|---------|
| 0x9C402000 | IOCTL_CE_READMEMORY | 读取进程内存 |
| 0x9C402004 | IOCTL_CE_WRITEMEMORY | 写入进程内存 |
| 0x9C402008 | IOCTL_CE_OPENPROCESS | 打开进程 |
| 0x9C40200C | IOCTL_CE_OPENTHREAD | 打开线程 |
| 0x9C402010 | IOCTL_CE_MAKEWRITABLE | 使内存可写 |
| 0x9C402014 | IOCTL_CE_QUERY_VIRTUAL_MEMORY | 查询虚拟内存 |
| 0x9C402034 | IOCTL_CE_GETPEPROCESS | 获取EPROCESS地址 |
| 0x9C402038 | IOCTL_CE_READPHYSICALMEMORY | 读取物理内存 |
| 0x9C4020C0 | IOCTL_CE_GETVERSION | 获取驱动版本 |

完整列表请参考 `IOPLDispatcher.c` 中的 `DispatchIoctl` 函数。

## 客户端实现

### Python客户端类

```python
class DBKSocketClient:
    def __init__(self, host="127.0.0.1", port=28996):
        self.host = host
        self.port = port
        self.sock = None
    
    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
    
    def disconnect(self):
        if self.sock:
            self.sock.close()
            self.sock = None
    
    def send_request(self, ioctl, input_data, output_size):
        # 发送消息头
        header = struct.pack("<IIII", ioctl, len(input_data), output_size, 0)
        self.sock.sendall(header)
        
        # 发送输入数据
        if input_data:
            self.sock.sendall(input_data)
        
        # 接收响应头
        resp_header = self._recv_exact(16)
        status, data_size, _, _ = struct.unpack("<iIII", resp_header)
        
        # 接收输出数据
        output_data = None
        if data_size > 0:
            output_data = self._recv_exact(data_size)
        
        return status, output_data
    
    def _recv_exact(self, size):
        data = b""
        while len(data) < size:
            chunk = self.sock.recv(size - len(data))
            if not chunk:
                raise ConnectionError("连接断开")
            data += chunk
        return data
```

### C++客户端类

```cpp
class DBKSocketClient {
private:
    SOCKET sock;
    
public:
    bool Connect(const char* host = "127.0.0.1", int port = 28996) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &addr.sin_addr);
        
        return connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0;
    }
    
    bool SendRequest(ULONG ioctl, void* input, ULONG inputSize,
                     void* output, ULONG outputSize, LONG* status) {
        // 发送消息头
        SOCKET_MESSAGE_HEADER header = {ioctl, inputSize, outputSize, 0};
        send(sock, (char*)&header, sizeof(header), 0);
        
        // 发送输入数据
        if (inputSize > 0)
            send(sock, (char*)input, inputSize, 0);
        
        // 接收响应头
        SOCKET_RESPONSE_HEADER resp;
        RecvExact(&resp, sizeof(resp));
        *status = resp.Status;
        
        // 接收输出数据
        if (resp.DataSize > 0)
            RecvExact(output, min(resp.DataSize, outputSize));
        
        return true;
    }
};
```

## 编译和部署

### 1. 编译驱动

```cmd
# 打开WDK命令提示符
cd C:\Users\RED\Desktop\Lee\DBKKernel

# 编译
msbuild DBK.sln /p:Configuration=Release /p:Platform=x64
```

### 2. 加载驱动

```cmd
# 以管理员身份运行
sc create DBK type= kernel binPath= C:\path\to\DBK.sys
sc start DBK
```

### 3. 验证驱动已加载

```cmd
# 检查驱动状态
sc query DBK

# 检查端口监听
netstat -ano | findstr 28996
```

应该看到类似输出：
```
TCP    127.0.0.1:28996        0.0.0.0:0              LISTENING
```

### 4. 测试连接

```bash
python SocketClient.py
```

## 故障排除

### 问题1: 端口未监听

**症状**: `netstat` 没有显示28996端口

**解决方案**:
1. 使用DebugView查看驱动日志
2. 检查是否有错误信息
3. 确认驱动已成功加载

### 问题2: 连接被拒绝

**症状**: `connect()` 返回错误10061

**可能原因**:
- 驱动未加载
- 端口被占用
- 防火墙阻止

**解决方案**:
```cmd
# 检查驱动状态
sc query DBK

# 检查端口占用
netstat -ano | findstr 28996

# 临时关闭防火墙测试
```

### 问题3: 通信超时

**症状**: `recv()` 一直阻塞

**解决方案**:
```python
# 设置超时
sock.settimeout(5.0)  # 5秒超时
```

## 安全注意事项

### 1. 仅本地访问

驱动只监听 `127.0.0.1`，不接受外部连接。

### 2. 权限要求

虽然不再使用设备对象，但驱动仍然需要管理员权限加载。

### 3. 单客户端限制

当前实现只允许一个客户端连接，新连接会被拒绝。

### 4. 无加密

当前版本不加密通信，敏感环境建议添加TLS。

## 性能优化建议

### 1. 保持长连接

```python
# 好的做法
client = DBKSocketClient()
client.connect()
for i in range(1000):
    client.get_version()
client.disconnect()

# 不好的做法
for i in range(1000):
    client = DBKSocketClient()
    client.connect()
    client.get_version()
    client.disconnect()
```

### 2. 批量操作

```python
# 一次读取大块内存
data = client.read_memory(pid, 0x400000, 0x10000)

# 而不是多次小读取
for addr in range(0x400000, 0x410000, 0x1000):
    data = client.read_memory(pid, addr, 0x1000)
```

## 总结

本驱动完全基于Socket通信，提供了：

✅ **跨语言支持** - 任何语言都可以编写客户端  
✅ **简单协议** - 易于理解和实现  
✅ **完整功能** - 保留所有原有IOCTL命令  
✅ **易于调试** - 可以使用网络工具抓包分析  

开始使用：
1. 编译并加载驱动
2. 连接到 `127.0.0.1:28996`
3. 发送请求，接收响应
4. 享受跨语言的内核通信！

