# DBK驱动Socket通信 - 快速开始指南

## 目录结构

```
DBKKernel/
├── DBK/
│   ├── SocketComm.h          # Socket通信头文件（新增）
│   ├── SocketComm.c          # Socket通信实现（新增）
│   ├── DBKDrvr.c             # 主驱动文件（已修改）
│   ├── DBKDrvr.h
│   ├── IOPLDispatcher.c
│   └── ... (其他原有文件)
├── SocketClient.py           # Python客户端示例（新增）
├── SocketClient.cpp          # C++客户端示例（新增）
├── SOCKET_README.md          # 详细文档（新增）
└── QUICKSTART.md             # 本文件（新增）
```

## 快速开始（5分钟上手）

### 步骤1: 修改项目配置

#### 方法A: 手动修改vcxproj文件

打开 `DBK/DBK.vcxproj`，在 `<ItemGroup>` 中添加：

```xml
<ItemGroup>
  <!-- 现有文件 -->
  <ClCompile Include="DBKDrvr.c" />
  <ClCompile Include="DBKFunc.c" />
  <!-- ... 其他文件 ... -->
  
  <!-- 新增Socket通信文件 -->
  <ClCompile Include="SocketComm.c" />
</ItemGroup>

<ItemGroup>
  <!-- 现有头文件 -->
  <ClInclude Include="DBKDrvr.h" />
  <ClInclude Include="DBKFunc.h" />
  <!-- ... 其他文件 ... -->
  
  <!-- 新增Socket通信头文件 -->
  <ClInclude Include="SocketComm.h" />
</ItemGroup>
```

在 `<ItemDefinitionGroup>` 中添加WSK库链接：

```xml
<Link>
  <AdditionalDependencies>
    $(DDK_LIB_PATH)\ntoskrnl.lib;
    $(DDK_LIB_PATH)\hal.lib;
    $(DDK_LIB_PATH)\wdmsec.lib;
    netio.lib;  <!-- 新增这一行 -->
    %(AdditionalDependencies)
  </AdditionalDependencies>
</Link>
```

#### 方法B: 使用Visual Studio界面

1. 在Visual Studio中打开 `DBK.sln`
2. 右键点击项目 → 添加 → 现有项
3. 选择 `SocketComm.c` 和 `SocketComm.h`
4. 右键点击项目 → 属性 → 链接器 → 输入
5. 在"附加依赖项"中添加 `netio.lib`

### 步骤2: 编译驱动

```cmd
# 打开WDK命令提示符
cd C:\Users\RED\Desktop\Lee\DBKKernel

# 编译x64版本
msbuild DBK.sln /p:Configuration=Release /p:Platform=x64

# 或编译x86版本
msbuild DBK.sln /p:Configuration=Release /p:Platform=Win32
```

编译成功后，驱动文件位于：
- x64: `DBK\x64\Release\DBK.sys`
- x86: `DBK\Win32\Release\DBK.sys`

### 步骤3: 加载驱动

**注意：需要管理员权限！**

```cmd
# 方法1: 使用sc命令（推荐）
sc create DBK type= kernel binPath= C:\path\to\DBK.sys
sc start DBK

# 方法2: 使用OSR Driver Loader工具
# 下载地址: https://www.osronline.com/article.cfm%5Earticle=157.htm
```

### 步骤4: 测试连接

#### 使用Python客户端

```bash
# 确保已安装Python 3.x
python SocketClient.py
```

预期输出：
```
============================================================
DBK驱动Socket通信客户端测试
============================================================
[+] 成功连接到驱动 127.0.0.1:28996

[测试1] 获取驱动版本
------------------------------------------------------------
[+] 驱动版本: 2000023
...
```

#### 使用C++客户端

```cmd
# 编译
cl SocketClient.cpp /link ws2_32.lib

# 运行
SocketClient.exe
```

## 常见问题排查

### 问题1: 驱动加载失败

**症状：** `sc start DBK` 返回错误

**解决方案：**
1. 检查是否以管理员权限运行
2. 禁用驱动签名强制（测试环境）：
   ```cmd
   bcdedit /set testsigning on
   # 重启电脑
   ```
3. 查看系统日志：
   ```cmd
   eventvwr.msc
   # 查看 Windows日志 → 系统
   ```

### 问题2: 客户端无法连接

**症状：** `连接失败: 10061` (WSAECONNREFUSED)

**解决方案：**
1. 确认驱动已成功加载：
   ```cmd
   sc query DBK
   ```
2. 检查端口是否监听：
   ```cmd
   netstat -ano | findstr 28996
   ```
3. 查看驱动日志（使用DebugView）：
   - 下载DebugView: https://docs.microsoft.com/sysinternals
   - 运行DebugView，查看是否有 `[SocketComm]` 开头的日志

### 问题3: 编译错误

**症状：** `无法解析的外部符号 WskRegister`

**解决方案：**
- 确保已添加 `netio.lib` 到链接器依赖项
- 检查WDK版本（需要Windows 7 WDK或更高版本）

### 问题4: 蓝屏 (BSOD)

**症状：** 加载驱动后系统蓝屏

**解决方案：**
1. 启用内核调试：
   ```cmd
   bcdedit /debug on
   bcdedit /dbgsettings serial debugport:1 baudrate:115200
   ```
2. 使用WinDbg连接内核调试
3. 检查蓝屏错误代码和调用栈

## 调试技巧

### 查看驱动日志

使用DebugView查看DbgPrint输出：

```cmd
# 下载并运行DebugView
Dbgview.exe

# 启用内核消息捕获
# Capture → Capture Kernel
```

关键日志标记：
- `[SocketComm]` - Socket通信相关
- `[DBK]` - 驱动主要功能
- `IOCTL_CE_` - IOCTL命令处理

### 网络抓包

使用Wireshark抓取本地回环流量：

```cmd
# 安装Npcap（支持回环抓包）
# https://npcap.com/

# 在Wireshark中选择 "Adapter for loopback traffic capture"
# 过滤器: tcp.port == 28996
```

### 性能测试

```python
import time
from SocketClient import DBKSocketClient

client = DBKSocketClient()
client.connect()

# 测试延迟
start = time.time()
for i in range(1000):
    client.get_version()
end = time.time()

print(f"1000次请求耗时: {end - start:.2f}秒")
print(f"平均延迟: {(end - start) / 1000 * 1000:.2f}毫秒")
```

## 安全配置

### 生产环境建议

1. **限制监听地址**
   
   修改 `SocketComm.h`：
   ```c
   // 只监听本地回环，不接受外部连接
   localAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
   ```

2. **添加访问控制**
   
   在 `SocketComm_AcceptEvent` 中添加：
   ```c
   // 检查客户端IP
   PSOCKADDR_IN remoteAddr = (PSOCKADDR_IN)RemoteAddress;
   if (remoteAddr->sin_addr.s_addr != inet_addr("127.0.0.1")) {
       return STATUS_REQUEST_NOT_ACCEPTED;
   }
   ```

3. **启用防火墙规则**
   ```cmd
   netsh advfirewall firewall add rule name="DBK Driver" ^
       dir=in action=allow protocol=TCP localport=28996 ^
       remoteip=127.0.0.1
   ```

## 性能优化

### 批量操作示例

```python
# 不推荐：多次单独调用
for addr in range(0x400000, 0x500000, 0x1000):
    data = client.read_process_memory(pid, addr, 16)

# 推荐：一次读取大块内存
data = client.read_process_memory(pid, 0x400000, 0x100000)
```

### 连接复用

```python
# 保持长连接
client = DBKSocketClient()
client.connect()

# 执行多个操作
client.get_version()
client.open_process(1234)
client.read_process_memory(1234, 0x400000, 64)

# 最后断开
client.disconnect()
```

## 下一步

- 阅读 [SOCKET_README.md](SOCKET_README.md) 了解详细架构
- 查看 [SocketClient.py](SocketClient.py) 学习Python API
- 查看 [SocketClient.cpp](SocketClient.cpp) 学习C++ API
- 根据需求扩展自定义功能

## 技术支持

遇到问题？

1. 查看本文档的"常见问题排查"部分
2. 检查驱动日志（DebugView）
3. 使用网络抓包工具（Wireshark）
4. 提交Issue到项目仓库

## 许可证

本项目基于原DBK驱动，遵循相应的开源许可证。


