#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DBK驱动Socket通信客户端
用于通过Socket与内核驱动进行通信，替代传统的IRP/DeviceIoControl方式
"""

import socket
import struct
import sys

# Socket配置
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 28996

# IOCTL代码定义（从原驱动中提取）
IOCTL_CE_READMEMORY = 0x9C402000
IOCTL_CE_WRITEMEMORY = 0x9C402004
IOCTL_CE_OPENPROCESS = 0x9C402008
IOCTL_CE_GETVERSION = 0x9C4020C0
IOCTL_CE_GETPEPROCESS = 0x9C402034
IOCTL_CE_READPHYSICALMEMORY = 0x9C402038

class DBKSocketClient:
    """DBK驱动Socket客户端"""
    
    def __init__(self, host=SERVER_HOST, port=SERVER_PORT):
        self.host = host
        self.port = port
        self.sock = None
        
    def connect(self):
        """连接到驱动Socket服务器"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port))
            print(f"[+] 成功连接到驱动 {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"[-] 连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开连接"""
        if self.sock:
            self.sock.close()
            self.sock = None
            print("[+] 已断开连接")
    
    def send_request(self, ioctl_code, input_data=b"", output_size=0):
        """
        发送请求到驱动
        
        参数:
            ioctl_code: IOCTL控制码
            input_data: 输入数据（bytes）
            output_size: 期望的输出数据大小
            
        返回:
            (status, output_data) 元组
        """
        if not self.sock:
            print("[-] 未连接到服务器")
            return None, None
        
        try:
            # 构造消息头
            # struct SOCKET_MESSAGE_HEADER {
            #     ULONG IoControlCode;
            #     ULONG InputBufferSize;
            #     ULONG OutputBufferSize;
            #     ULONG Reserved;
            # }
            input_size = len(input_data)
            header = struct.pack("<IIII", ioctl_code, input_size, output_size, 0)
            
            # 发送消息头
            self.sock.sendall(header)
            
            # 发送输入数据
            if input_size > 0:
                self.sock.sendall(input_data)
            
            # 接收响应头
            # struct SOCKET_RESPONSE_HEADER {
            #     NTSTATUS Status;
            #     ULONG DataSize;
            #     ULONG Reserved1;
            #     ULONG Reserved2;
            # }
            resp_header = self._recv_exact(16)
            if not resp_header:
                print("[-] 接收响应头失败")
                return None, None
            
            status, data_size, _, _ = struct.unpack("<iIII", resp_header)
            
            # 接收响应数据
            output_data = b""
            if data_size > 0:
                output_data = self._recv_exact(data_size)
                if not output_data:
                    print("[-] 接收响应数据失败")
                    return status, None
            
            return status, output_data
            
        except Exception as e:
            print(f"[-] 发送请求失败: {e}")
            return None, None
    
    def _recv_exact(self, size):
        """接收指定大小的数据"""
        data = b""
        while len(data) < size:
            chunk = self.sock.recv(size - len(data))
            if not chunk:
                return None
            data += chunk
        return data
    
    def get_version(self):
        """获取驱动版本"""
        status, data = self.send_request(IOCTL_CE_GETVERSION, b"", 4)
        if status == 0 and data:
            version = struct.unpack("<I", data)[0]
            print(f"[+] 驱动版本: {version}")
            return version
        else:
            print(f"[-] 获取版本失败，状态码: 0x{status:08X}")
            return None
    
    def open_process(self, process_id):
        """打开进程"""
        input_data = struct.pack("<I", process_id)
        status, data = self.send_request(IOCTL_CE_OPENPROCESS, input_data, 9)
        
        if status == 0 and data:
            handle, special = struct.unpack("<QB", data)
            print(f"[+] 打开进程 PID={process_id} 成功")
            print(f"    句柄: 0x{handle:016X}")
            print(f"    特殊标志: {special}")
            return handle
        else:
            print(f"[-] 打开进程失败，状态码: 0x{status:08X}")
            return None
    
    def read_process_memory(self, process_id, address, size):
        """读取进程内存"""
        # struct input {
        #     UINT64 processid;
        #     UINT64 startaddress;
        #     WORD bytestoread;
        # }
        input_data = struct.pack("<QQH", process_id, address, size)
        status, data = self.send_request(IOCTL_CE_READMEMORY, input_data, size + 18)
        
        if status == 0 and data:
            # 跳过输入结构，获取实际读取的数据
            memory_data = data[18:18+size]
            print(f"[+] 读取内存成功: PID={process_id}, 地址=0x{address:X}, 大小={size}")
            return memory_data
        else:
            print(f"[-] 读取内存失败，状态码: 0x{status:08X}")
            return None
    
    def write_process_memory(self, process_id, address, data):
        """写入进程内存"""
        # struct input {
        #     UINT64 processid;
        #     UINT64 startaddress;
        #     WORD bytestowrite;
        # }
        size = len(data)
        input_data = struct.pack("<QQH", process_id, address, size) + data
        status, _ = self.send_request(IOCTL_CE_WRITEMEMORY, input_data, 0)
        
        if status == 0:
            print(f"[+] 写入内存成功: PID={process_id}, 地址=0x{address:X}, 大小={size}")
            return True
        else:
            print(f"[-] 写入内存失败，状态码: 0x{status:08X}")
            return False
    
    def get_peprocess(self, process_id):
        """获取EPROCESS结构地址"""
        input_data = struct.pack("<I", process_id)
        status, data = self.send_request(IOCTL_CE_GETPEPROCESS, input_data, 8)
        
        if status == 0 and data:
            peprocess = struct.unpack("<Q", data)[0]
            print(f"[+] EPROCESS地址: 0x{peprocess:016X}")
            return peprocess
        else:
            print(f"[-] 获取EPROCESS失败，状态码: 0x{status:08X}")
            return None


def print_hex_dump(data, address=0):
    """打印十六进制数据"""
    for i in range(0, len(data), 16):
        hex_str = " ".join(f"{b:02X}" for b in data[i:i+16])
        ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in data[i:i+16])
        print(f"  {address+i:08X}: {hex_str:<48} {ascii_str}")


def main():
    """主函数 - 测试示例"""
    print("=" * 60)
    print("DBK驱动Socket通信客户端测试")
    print("=" * 60)
    
    # 创建客户端
    client = DBKSocketClient()
    
    # 连接到驱动
    if not client.connect():
        return
    
    try:
        # 测试1: 获取驱动版本
        print("\n[测试1] 获取驱动版本")
        print("-" * 60)
        version = client.get_version()
        
        # 测试2: 打开当前进程
        print("\n[测试2] 打开当前进程")
        print("-" * 60)
        import os
        current_pid = os.getpid()
        print(f"当前进程PID: {current_pid}")
        handle = client.open_process(current_pid)
        
        # 测试3: 获取EPROCESS
        print("\n[测试3] 获取EPROCESS结构")
        print("-" * 60)
        peprocess = client.get_peprocess(current_pid)
        
        # 测试4: 读取内存（示例：读取进程自身的一些内存）
        print("\n[测试4] 读取进程内存")
        print("-" * 60)
        # 这里只是示例，实际地址需要根据具体情况调整
        test_address = 0x400000  # 典型的PE文件基址
        memory = client.read_process_memory(current_pid, test_address, 64)
        if memory:
            print("内存数据:")
            print_hex_dump(memory, test_address)
        
        print("\n" + "=" * 60)
        print("测试完成！")
        print("=" * 60)
        
    except KeyboardInterrupt:
        print("\n[!] 用户中断")
    except Exception as e:
        print(f"\n[-] 发生错误: {e}")
        import traceback
        traceback.print_exc()
    finally:
        # 断开连接
        client.disconnect()


if __name__ == "__main__":
    main()


