#!/bin/bash
# ============================================================
# DBK驱动 - Socket通信集成脚本 (Linux/WSL版本)
# 自动将Socket通信功能集成到现有DBK项目
# ============================================================

echo "============================================================"
echo "DBK驱动 Socket通信集成工具"
echo "============================================================"
echo ""

# 设置项目路径
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)/"
DBK_DIR="${PROJECT_DIR}DBK"

echo "[1/5] 检查项目结构..."
if [ ! -d "$DBK_DIR" ]; then
    echo "[错误] 找不到DBK目录: $DBK_DIR"
    exit 1
fi

if [ ! -f "$DBK_DIR/DBKDrvr.c" ]; then
    echo "[错误] 找不到DBKDrvr.c文件"
    exit 1
fi

echo "[OK] 项目结构检查通过"
echo ""

echo "[2/5] 检查Socket通信文件..."
if [ ! -f "$DBK_DIR/SocketComm.h" ]; then
    echo "[错误] 找不到SocketComm.h"
    echo "请确保已将SocketComm.h和SocketComm.c复制到DBK目录"
    exit 1
fi

if [ ! -f "$DBK_DIR/SocketComm.c" ]; then
    echo "[错误] 找不到SocketComm.c"
    echo "请确保已将SocketComm.h和SocketComm.c复制到DBK目录"
    exit 1
fi

echo "[OK] Socket通信文件检查通过"
echo ""

echo "[3/5] 备份原始文件..."
mkdir -p "${PROJECT_DIR}backup"
cp "$DBK_DIR/DBKDrvr.c" "${PROJECT_DIR}backup/DBKDrvr.c.bak"
cp "$DBK_DIR/DBK.vcxproj" "${PROJECT_DIR}backup/DBK.vcxproj.bak" 2>/dev/null || true
echo "[OK] 备份完成: backup/DBKDrvr.c.bak"
echo ""

echo "[4/5] 检查DBKDrvr.c是否已修改..."
if grep -q "SocketComm.h" "$DBK_DIR/DBKDrvr.c"; then
    echo "[警告] DBKDrvr.c似乎已经包含Socket通信代码"
    echo "是否继续？这可能导致重复包含 [y/N]"
    read -r continue
    if [ "$continue" != "y" ] && [ "$continue" != "Y" ]; then
        echo "操作已取消"
        exit 0
    fi
else
    echo "[OK] DBKDrvr.c尚未修改，可以继续"
fi
echo ""

echo "[5/5] 自动应用补丁..."

# 检查是否已经包含SocketComm.h
if ! grep -q '#include "SocketComm.h"' "$DBK_DIR/DBKDrvr.c"; then
    # 在noexceptions.h之后添加SocketComm.h
    sed -i '/#include "noexceptions.h"/a #include "SocketComm.h"' "$DBK_DIR/DBKDrvr.c"
    echo "[OK] 已添加 #include \"SocketComm.h\""
else
    echo "[跳过] SocketComm.h已包含"
fi

# 在UnloadDriver中添加清理代码
if ! grep -q "SocketComm_Cleanup" "$DBK_DIR/DBKDrvr.c"; then
    sed -i '/ultimap_disable();/i \\tDbgPrint("Cleaning up Socket Communication...\\n");\n\tSocketComm_Cleanup();\n' "$DBK_DIR/DBKDrvr.c"
    echo "[OK] 已在UnloadDriver中添加清理代码"
else
    echo "[跳过] UnloadDriver已包含清理代码"
fi

# 在DriverEntry末尾添加初始化代码
if ! grep -q "SocketComm_Initialize" "$DBK_DIR/DBKDrvr.c"; then
    # 在最后的return STATUS_SUCCESS之前添加
    sed -i '/return STATUS_SUCCESS;/i \\n\t// 初始化Socket通信\n\tDbgPrint("Initializing Socket Communication...\\n");\n\tntStatus = SocketComm_Initialize();\n\tif (NT_SUCCESS(ntStatus)) {\n\t\tntStatus = SocketComm_StartListening();\n\t\tif (NT_SUCCESS(ntStatus)) {\n\t\t\tDbgPrint("Socket listener started on port %d\\n", SOCKET_SERVER_PORT);\n\t\t}\n\t}\n' "$DBK_DIR/DBKDrvr.c"
    echo "[OK] 已在DriverEntry中添加初始化代码"
else
    echo "[跳过] DriverEntry已包含初始化代码"
fi

echo ""
echo "============================================================"
echo "自动集成完成！"
echo "============================================================"
echo ""
echo "下一步："
echo "1. 手动编辑 DBK/DBK.vcxproj，添加以下内容："
echo ""
echo "   在 <ItemGroup> 中添加："
echo "     <ClCompile Include=\"SocketComm.c\" />"
echo "     <ClInclude Include=\"SocketComm.h\" />"
echo ""
echo "   在 <Link> 的 <AdditionalDependencies> 中添加："
echo "     netio.lib;"
echo ""
echo "2. 编译驱动："
echo "   msbuild DBK.sln /p:Configuration=Release /p:Platform=x64"
echo ""
echo "3. 测试："
echo "   sc create DBK type= kernel binPath= C:\\path\\to\\DBK.sys"
echo "   sc start DBK"
echo "   python SocketClient.py"
echo ""
echo "备份文件位于: ${PROJECT_DIR}backup/"
echo "============================================================"


