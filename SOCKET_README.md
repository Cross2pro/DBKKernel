# DBK驱动 - Socket通信实现

## 概述

本项目将原有的DBK内核驱动从传统的IRP（I/O Request Packet）通信方式改为**本机Socket通信**方式。这种改变提供了更灵活的通信机制，并且可以支持跨语言、跨平台的客户端开发。

## 架构说明

### 原有架构（IRP通信）
```
用户态应用
    ↓ (DeviceIoControl)
设备对象 (\\.\DeviceName)
    ↓ (IRP_MJ_DEVICE_CONTROL)
DispatchIoctl函数
    ↓
处理各种IOCTL命令
```

### 新架构（Socket通信）
```
用户态应用
    ↓ (TCP Socket)
本地端口 28996
    ↓ (WSK - Winsock Kernel)
SocketComm模块
    ↓
DispatchIoctl函数（复用原有逻辑）
    ↓
处理各种IOCTL命令
```

## 文件说明

### 驱动端文件

1. **SocketComm.h** - Socket通信头文件
   - 定义Socket通信上下文结构
   - 定义消息头和响应头结构
   - 声明Socket通信相关函数

2. **SocketComm.c** - Socket通信实现
   - 使用WSK (Winsock Kernel) API实现内核Socket
   - 监听本地端口28996
   - 接收客户端请求并调用原有的DispatchIoctl逻辑
   - 返回处理结果给客户端

3. **DBKDrvr.c** (已修改)
   - 在DriverEntry中初始化Socket通信
   - 在UnloadDriver中清理Socket通信

### 客户端文件

1. **SocketClient.py** - Python客户端
   - 使用标准socket库
   - 提供简单易用的API接口
   - 包含测试示例

2. **SocketClient.cpp** - C++客户端
   - 使用Winsock2 API
   - 提供面向对象的封装
   - 包含测试示例

## 通信协议

### 请求格式

```c
// 消息头 (16字节)
struct SOCKET_MESSAGE_HEADER {
    ULONG IoControlCode;      // IOCTL代码 (4字节)
    ULONG InputBufferSize;    // 输入数据大小 (4字节)
    ULONG OutputBufferSize;   // 期望输出大小 (4字节)
    ULONG Reserved;           // 保留字段 (4字节)
};

// 消息体
BYTE InputData[InputBufferSize];  // 输入数据
```

### 响应格式

```c
// 响应头 (16字节)
struct SOCKET_RESPONSE_HEADER {
    LONG Status;              // NTSTATUS状态码 (4字节)
    ULONG DataSize;           // 返回数据大小 (4字节)
    ULONG Reserved1;          // 保留字段 (4字节)
    ULONG Reserved2;          // 保留字段 (4字节)
};

// 响应体
BYTE OutputData[DataSize];    // 输出数据
```

## 编译说明

### 驱动编译

1. 将新文件添加到项目：
   - SocketComm.h
   - SocketComm.c

2. 修改项目设置：
   - 添加WSK库链接：`netio.lib`
   - 确保包含WDK头文件路径

3. 使用WDK编译驱动：
```cmd
msbuild DBK.vcxproj /p:Configuration=Release /p:Platform=x64
```

### 客户端编译

**Python客户端：**
```bash
# 无需编译，直接运行
python SocketClient.py
```

**C++客户端：**
```cmd
# 使用Visual Studio编译
cl SocketClient.cpp /link ws2_32.lib

# 或使用g++
g++ SocketClient.cpp -o SocketClient.exe -lws2_32
```

## 使用方法

### 1. 加载驱动

```cmd
sc create DBK type= kernel binPath= C:\path\to\DBK.sys
sc start DBK
```

### 2. 运行客户端

**Python：**
```bash
python SocketClient.py
```

**C++：**
```cmd
SocketClient.exe
```

### 3. API使用示例

**Python示例：**
```python
from SocketClient import DBKSocketClient

# 创建客户端并连接
client = DBKSocketClient()
client.connect()

# 获取驱动版本
version = client.get_version()

# 打开进程
handle = client.open_process(1234)

# 读取内存
data = client.read_process_memory(1234, 0x400000, 64)

# 断开连接
client.disconnect()
```

**C++示例：**
```cpp
#include "SocketClient.cpp"

int main() {
    DBKSocketClient client;
    
    // 连接到驱动
    if (!client.Connect()) {
        return 1;
    }
    
    // 获取驱动版本
    ULONG version;
    client.GetVersion(&version);
    
    // 打开进程
    UINT64 handle;
    client.OpenProcess(1234, &handle);
    
    // 读取内存
    BYTE buffer[64];
    client.ReadProcessMemory(1234, 0x400000, 64, buffer);
    
    // 断开连接
    client.Disconnect();
    
    return 0;
}
```

## 支持的IOCTL命令

所有原有的IOCTL命令都被保留并支持，包括但不限于：

- `IOCTL_CE_READMEMORY` (0x9C402000) - 读取进程内存
- `IOCTL_CE_WRITEMEMORY` (0x9C402004) - 写入进程内存
- `IOCTL_CE_OPENPROCESS` (0x9C402008) - 打开进程
- `IOCTL_CE_GETVERSION` (0x9C4020C0) - 获取驱动版本
- `IOCTL_CE_GETPEPROCESS` (0x9C402034) - 获取EPROCESS地址
- `IOCTL_CE_READPHYSICALMEMORY` (0x9C402038) - 读取物理内存
- ... 以及其他所有原有命令

## 优势

### 相比IRP通信的优势：

1. **跨语言支持** - 任何支持Socket的语言都可以编写客户端
2. **网络透明** - 理论上可以支持远程通信（需要额外的安全措施）
3. **调试方便** - 可以使用Wireshark等工具抓包分析
4. **灵活性高** - 不依赖Windows设备驱动框架
5. **并发支持** - 可以轻松扩展为支持多客户端

### 相比原有方式的劣势：

1. **性能开销** - Socket通信比直接IRP调用有额外开销
2. **安全性** - 需要额外考虑端口安全和访问控制
3. **复杂度** - 实现相对复杂，需要处理网络异常

## 安全注意事项

1. **端口安全** - 默认监听本地端口28996，建议只绑定到127.0.0.1
2. **权限检查** - 驱动仍然保留SeDebugPrivilege检查
3. **防火墙** - 确保防火墙规则正确配置
4. **加密通信** - 当前版本未加密，敏感环境建议添加TLS支持

## 故障排除

### 驱动无法加载
- 检查是否以管理员权限运行
- 检查驱动签名是否正确
- 查看系统日志：`eventvwr.msc`

### 客户端无法连接
- 确认驱动已成功加载
- 检查端口28996是否被占用：`netstat -ano | findstr 28996`
- 检查防火墙设置

### 通信超时
- 增加Socket超时时间
- 检查网络状态
- 查看驱动日志：使用DebugView工具

## 调试技巧

### 驱动端调试
```cmd
# 使用WinDbg连接内核调试
bcdedit /debug on
bcdedit /dbgsettings serial debugport:1 baudrate:115200

# 查看驱动日志
DbgView.exe
```

### 客户端调试
```python
# Python - 启用详细日志
import logging
logging.basicConfig(level=logging.DEBUG)
```

```cpp
// C++ - 添加调试输出
#define DEBUG_PRINT(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
```

## 性能优化建议

1. **批量操作** - 合并多个小请求为一个大请求
2. **缓冲区复用** - 重用发送/接收缓冲区
3. **异步处理** - 使用异步Socket操作
4. **连接池** - 保持长连接，避免频繁建立连接

## 扩展功能建议

1. **多客户端支持** - 修改为支持多个并发连接
2. **TLS加密** - 添加SSL/TLS支持保护通信
3. **认证机制** - 添加客户端认证
4. **压缩传输** - 对大数据传输进行压缩
5. **心跳机制** - 添加心跳包检测连接状态

## 许可证

本项目基于原DBK驱动项目，请遵守相应的许可证条款。

## 联系方式

如有问题或建议，请通过以下方式联系：
- 提交Issue到项目仓库
- 发送邮件到开发者

## 更新日志

### v1.0.0 (2026-02-09)
- 初始版本
- 实现基本的Socket通信功能
- 提供Python和C++客户端示例
- 完整保留原有IOCTL功能


