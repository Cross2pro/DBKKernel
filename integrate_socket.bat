@echo off
REM ============================================================
REM DBK驱动 - Socket通信集成脚本
REM 自动将Socket通信功能集成到现有DBK项目
REM ============================================================

echo ============================================================
echo DBK驱动 Socket通信集成工具
echo ============================================================
echo.

REM 检查管理员权限
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [错误] 需要管理员权限运行此脚本
    echo 请右键点击脚本，选择"以管理员身份运行"
    pause
    exit /b 1
)

REM 设置项目路径
set PROJECT_DIR=%~dp0
set DBK_DIR=%PROJECT_DIR%DBK

echo [1/5] 检查项目结构...
if not exist "%DBK_DIR%" (
    echo [错误] 找不到DBK目录: %DBK_DIR%
    pause
    exit /b 1
)

if not exist "%DBK_DIR%\DBKDrvr.c" (
    echo [错误] 找不到DBKDrvr.c文件
    pause
    exit /b 1
)

echo [OK] 项目结构检查通过
echo.

echo [2/5] 检查Socket通信文件...
if not exist "%DBK_DIR%\SocketComm.h" (
    echo [错误] 找不到SocketComm.h
    echo 请确保已将SocketComm.h和SocketComm.c复制到DBK目录
    pause
    exit /b 1
)

if not exist "%DBK_DIR%\SocketComm.c" (
    echo [错误] 找不到SocketComm.c
    echo 请确保已将SocketComm.h和SocketComm.c复制到DBK目录
    pause
    exit /b 1
)

echo [OK] Socket通信文件检查通过
echo.

echo [3/5] 备份原始文件...
if not exist "%PROJECT_DIR%backup" mkdir "%PROJECT_DIR%backup"
copy "%DBK_DIR%\DBKDrvr.c" "%PROJECT_DIR%backup\DBKDrvr.c.bak" >nul
copy "%DBK_DIR%\DBK.vcxproj" "%PROJECT_DIR%backup\DBK.vcxproj.bak" >nul
echo [OK] 备份完成: backup\DBKDrvr.c.bak
echo [OK] 备份完成: backup\DBK.vcxproj.bak
echo.

echo [4/5] 检查DBKDrvr.c是否已修改...
findstr /C:"SocketComm.h" "%DBK_DIR%\DBKDrvr.c" >nul 2>&1
if %errorLevel% equ 0 (
    echo [警告] DBKDrvr.c似乎已经包含Socket通信代码
    echo 是否继续？这可能导致重复包含 [Y/N]
    set /p continue=
    if /i not "%continue%"=="Y" (
        echo 操作已取消
        pause
        exit /b 0
    )
) else (
    echo [OK] DBKDrvr.c尚未修改，可以继续
)
echo.

echo [5/5] 显示集成说明...
echo.
echo ============================================================
echo 手动集成步骤（请按照以下步骤操作）
echo ============================================================
echo.
echo 步骤1: 修改 DBK\DBKDrvr.c
echo ----------------------------------------
echo 在文件顶部的 #include 部分添加：
echo     #include "SocketComm.h"
echo.
echo 在 DriverEntry 函数的最后（return STATUS_SUCCESS 之前）添加：
echo     // 初始化Socket通信
echo     DbgPrint("Initializing Socket Communication...\n");
echo     ntStatus = SocketComm_Initialize();
echo     if (NT_SUCCESS(ntStatus)) {
echo         ntStatus = SocketComm_StartListening();
echo         if (NT_SUCCESS(ntStatus)) {
echo             DbgPrint("Socket listener started on port %%d\n", SOCKET_SERVER_PORT);
echo         }
echo     }
echo.
echo 在 UnloadDriver 函数的开始部分添加：
echo     DbgPrint("Cleaning up Socket Communication...\n");
echo     SocketComm_Cleanup();
echo.
echo ----------------------------------------
echo.
echo 步骤2: 修改 DBK\DBK.vcxproj
echo ----------------------------------------
echo 方法A - 使用Visual Studio（推荐）：
echo   1. 在Visual Studio中打开 DBK.sln
echo   2. 右键点击DBK项目 -^> 添加 -^> 现有项
echo   3. 选择 SocketComm.c 和 SocketComm.h
echo   4. 右键点击项目 -^> 属性 -^> 链接器 -^> 输入
echo   5. 在"附加依赖项"中添加: netio.lib
echo.
echo 方法B - 手动编辑vcxproj文件：
echo   在 ^<ItemGroup^> 中添加：
echo     ^<ClCompile Include="SocketComm.c" /^>
echo     ^<ClInclude Include="SocketComm.h" /^>
echo.
echo   在 ^<Link^> 的 ^<AdditionalDependencies^> 中添加：
echo     netio.lib;
echo.
echo ----------------------------------------
echo.
echo 步骤3: 编译驱动
echo ----------------------------------------
echo 打开WDK命令提示符，执行：
echo     cd %PROJECT_DIR%
echo     msbuild DBK.sln /p:Configuration=Release /p:Platform=x64
echo.
echo ----------------------------------------
echo.
echo 步骤4: 测试
echo ----------------------------------------
echo 1. 加载驱动：
echo     sc create DBK type= kernel binPath= C:\path\to\DBK.sys
echo     sc start DBK
echo.
echo 2. 运行测试客户端：
echo     python SocketClient.py
echo.
echo ============================================================
echo.

echo 是否现在打开DBKDrvr.c进行编辑？ [Y/N]
set /p edit=
if /i "%edit%"=="Y" (
    start notepad "%DBK_DIR%\DBKDrvr.c"
)

echo.
echo 是否现在打开DBK.vcxproj进行编辑？ [Y/N]
set /p edit_proj=
if /i "%edit_proj%"=="Y" (
    start notepad "%DBK_DIR%\DBK.vcxproj"
)

echo.
echo ============================================================
echo 集成准备完成！
echo 请按照上述步骤完成手动集成
echo 备份文件位于: %PROJECT_DIR%backup\
echo ============================================================
echo.
pause


