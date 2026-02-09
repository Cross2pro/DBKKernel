#pragma warning( disable: 4103)

#include "DBKFunc.h"
#include <ntddk.h>
#include <windef.h>
#include "DBKDrvr.h"

#include "deepkernel.h"
#include "processlist.h"
#include "memscan.h"
#include "threads.h"
#include "vmxhelper.h"
#include "debugger.h"


#include "IOPLDispatcher.h"
#include "interruptHook.h"
#include "ultimap.h"
#include "ultimap2.h"
#include "noexceptions.h"
#include "SocketComm.h"



void UnloadDriver(PDRIVER_OBJECT DriverObject);

NTSTATUS DispatchCreate(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS DispatchClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

#ifndef AMD64
//no api hooks for x64

//-----NtUserSetWindowsHookEx----- //prevent global hooks
typedef ULONG (NTUSERSETWINDOWSHOOKEX)(
    IN HANDLE hmod,
    IN PUNICODE_STRING pstrLib OPTIONAL,
    IN DWORD idThread,
    IN int nFilterType,
    IN PVOID pfnFilterProc,
    IN DWORD dwFlags
);
NTUSERSETWINDOWSHOOKEX OldNtUserSetWindowsHookEx;
ULONG NtUserSetWindowsHookEx_callnumber;
//HHOOK NewNtUserSetWindowsHookEx(IN HANDLE hmod,IN PUNICODE_STRING pstrLib OPTIONAL,IN DWORD idThread,IN int nFilterType, IN PROC pfnFilterProc,IN DWORD dwFlags);


typedef NTSTATUS (*ZWSUSPENDPROCESS)
(
    IN ULONG ProcessHandle  // Handle to the process
);
ZWSUSPENDPROCESS ZwSuspendProcess;

NTSTATUS ZwCreateThread(
	OUT PHANDLE  ThreadHandle,
	IN ACCESS_MASK  DesiredAccess,
	IN POBJECT_ATTRIBUTES  ObjectAttributes,
	IN HANDLE  ProcessHandle,
	OUT PCLIENT_ID  ClientId,
	IN PCONTEXT  ThreadContext,
	IN PVOID  UserStack,
	IN BOOLEAN  CreateSuspended);

//PVOID GetApiEntry(ULONG FunctionNumber);
#endif






// 注意：不再使用设备对象和符号链接
// UNICODE_STRING  uszDeviceString;  // 已移除
// PVOID BufDeviceString=NULL;       // 已移除



void hideme(PDRIVER_OBJECT DriverObject)
{
#ifndef AMD64
	
	typedef struct _MODULE_ENTRY {
	LIST_ENTRY le_mod;
	DWORD  unknown[4];
	DWORD  base;
	DWORD  driver_start;
	DWORD  unk1;
	UNICODE_STRING driver_Path;
	UNICODE_STRING driver_Name;
} MODULE_ENTRY, *PMODULE_ENTRY;

	PMODULE_ENTRY pm_current;

	pm_current =  *((PMODULE_ENTRY*)((DWORD)DriverObject + 0x14)); //eeeeew

	*((PDWORD)pm_current->le_mod.Blink)        = (DWORD) pm_current->le_mod.Flink;
	pm_current->le_mod.Flink->Blink            = pm_current->le_mod.Blink;
	HiddenDriver=TRUE;

#endif
}


int testfunction(int p1,int p2)
{
	DbgPrint("Hello\nParam1=%d\nParam2=%d\n",p1,p2);
	


	return 0x666;
}


void* functionlist[1];
char  paramsizes[1];
int registered=0;

VOID TestPassive(UINT_PTR param)
{
	DbgPrint("passive cpu call for cpu %d\n", KeGetCurrentProcessorNumber());
}


VOID TestDPC(IN struct _KDPC *Dpc, IN PVOID  DeferredContext, IN PVOID  SystemArgument1, IN PVOID  SystemArgument2)
{
	EFLAGS e=getEflags();
	
    DbgPrint("Defered cpu call for cpu %d (Dpc=%p  IF=%d IRQL=%d)\n", KeGetCurrentProcessorNumber(), Dpc, e.IF, KeGetCurrentIrql());
}


VOID TestThread(__in PVOID StartContext)
{
	PEPROCESS x = (PEPROCESS)StartContext;
	DbgPrint("Hello from testthread");

	//PsSuspendProcess((PEPROCESS)StartContext);

	

	DbgPrint("x=%p\n", x);


	
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject,
                     IN PUNICODE_STRING RegistryPath)
/*++

Routine Description:

    This routine is called when the driver is loaded by NT.
    注意：本驱动使用Socket通信，不创建设备对象，不使用IRP通信。

Arguments:

    DriverObject - Pointer to driver object created by system.
    RegistryPath - Pointer to the name of the services node for this driver.

Return Value:

    The function value is the final status from the initialization operation.

--*/
{
	
	
    NTSTATUS        ntStatus;
    PVOID           BufProcessEventString=NULL,BufThreadEventString=NULL;
    
    UNICODE_STRING  uszProcessEventString;
	UNICODE_STRING	uszThreadEventString;
	HANDLE reg=0;
	OBJECT_ATTRIBUTES oa;

	UNICODE_STRING temp; 
	char wbuf[100]; 
	WORD this_cs, this_ss, this_ds, this_es, this_fs, this_gs;
	ULONG cr4reg;

	
	
	criticalSection csTest;

	HANDLE Ultimap2Handle;

	
	KernelCodeStepping=0;
	KernelWritesIgnoreWP = 0;

	

	this_cs=getCS();
	this_ss=getSS();
	this_ds=getDS();
	this_es=getES();
	this_fs=getFS();
	this_gs=getGS();	



	//InitializeObjectAttributes(&ao, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
	//PsCreateSystemThread(&Ultimap2Handle, 0, NULL, 0, NULL, TestThread, PsGetCurrentProcess());

	DbgPrint("DBK loading...");

	//lame antiviruses and more lamer users that keep crying rootkit virus....
	temp.Buffer=(PWCH)wbuf;
	temp.Length=0;
	temp.MaximumLength=100;
	
	RtlAppendUnicodeToString(&temp, L"Ke"); //KeServiceDescriptorTable 
	RtlAppendUnicodeToString(&temp, L"Service");
	RtlAppendUnicodeToString(&temp, L"Descriptor");
	RtlAppendUnicodeToString(&temp, L"Table");
	
	KeServiceDescriptorTable=MmGetSystemRoutineAddress(&temp);         

	DbgPrint("Loading driver (Socket Communication Mode - No Device Object)\n");
	
	// 注意：不再创建设备对象，不再使用IRP通信
	// 所有通信通过Socket完成
	
	if (RegistryPath)
	{	
		DbgPrint("Registry path = %S\n", RegistryPath->Buffer);

		InitializeObjectAttributes(&oa,RegistryPath,OBJ_KERNEL_HANDLE ,NULL,NULL);
		ntStatus=ZwOpenKey(&reg,KEY_QUERY_VALUE,&oa);
		if (ntStatus == STATUS_SUCCESS)
		{
			UNICODE_STRING C,D;
			PKEY_VALUE_PARTIAL_INFORMATION bufC,bufD;
			ULONG ActualSize;

			DbgPrint("Opened the key\n");

			BufProcessEventString=ExAllocatePool(PagedPool,sizeof(KEY_VALUE_PARTIAL_INFORMATION)+100);
			BufThreadEventString=ExAllocatePool(PagedPool,sizeof(KEY_VALUE_PARTIAL_INFORMATION)+100);

			bufC=BufProcessEventString;
			bufD=BufThreadEventString;

			RtlInitUnicodeString(&C, L"C");
			RtlInitUnicodeString(&D, L"D");

			if (ntStatus == STATUS_SUCCESS)
				ntStatus=ZwQueryValueKey(reg,&C,KeyValuePartialInformation ,bufC,sizeof(KEY_VALUE_PARTIAL_INFORMATION)+100,&ActualSize);
			if (ntStatus == STATUS_SUCCESS)
				ntStatus=ZwQueryValueKey(reg,&D,KeyValuePartialInformation ,bufD,sizeof(KEY_VALUE_PARTIAL_INFORMATION)+100,&ActualSize);

			if (ntStatus == STATUS_SUCCESS)
			{
				DbgPrint("Read ok\n");
				RtlInitUnicodeString(&uszProcessEventString,(PCWSTR) bufC->Data);
				RtlInitUnicodeString(&uszThreadEventString,(PCWSTR) bufD->Data);

				DbgPrint("ProcessEventString=%S\n",uszProcessEventString.Buffer);
				DbgPrint("ThreadEventString=%S\n",uszThreadEventString.Buffer);
			}
			else
			{
				ExFreePool(bufC);
				ExFreePool(bufD);

				DbgPrint("Failed reading the value\n");
				ZwClose(reg);
				return STATUS_UNSUCCESSFUL;;
			}

		}
		else
		{
			DbgPrint("Failed opening the key\n");
			return STATUS_UNSUCCESSFUL;;
		}
	}
	else
	  loadedbydbvm=TRUE;

	ntStatus = STATUS_SUCCESS;

	DbgPrint("DriverObject=%p\n", DriverObject);

    // 设置卸载函数
    DriverObject->DriverUnload = UnloadDriver;
	
	// 不再设置IRP处理函数，因为不使用设备对象



	//Processlist init
#ifndef CETC
	ProcessEventCount=0;
	ExInitializeResourceLite(&ProcesslistR);	
#endif

	CreateProcessNotifyRoutineEnabled=FALSE;

	//threadlist init
	ThreadEventCount=0;
	
	BufferSize=0;
	processlist=NULL;

#ifndef AMD64
    //determine if PAE is used
	cr4reg=(ULONG)getCR4();

	if ((cr4reg & 0x20)==0x20)
	{
		PTESize=8; //pae
		PAGE_SIZE_LARGE=0x200000;
		MAX_PDE_POS=0xC0604000;
		MAX_PTE_POS=0xC07FFFF8;

		
	}
	else
	{
		PTESize=4;
		PAGE_SIZE_LARGE=0x400000;
		MAX_PDE_POS=0xC0301000;
		MAX_PTE_POS=0xC03FFFFC;
	}
#else
	PTESize=8; //pae
	PAGE_SIZE_LARGE=0x200000;
	//base was 0xfffff68000000000ULL

	//to 
	MAX_PTE_POS=0xFFFFF6FFFFFFFFF8ULL; // base + 0x7FFFFFFFF8
	MAX_PDE_POS=0xFFFFF6FB7FFFFFF8ULL; // base + 0x7B7FFFFFF8
#endif

	

#ifdef CETC
	DbgPrint("Going to initialice CETC\n");
	InitializeCETC();
#endif


    //hideme(DriverObject); //ok, for those that see this, enabling this WILL fuck up try except routines, even in usermode you'll get a blue sreen

	DbgPrint("Initializing debugger\n");
	debugger_initialize();


	// 清理初始化缓冲区
	DbgPrint("Cleaning up initialization buffers\n");

	if (BufProcessEventString)
	{
		ExFreePool(BufProcessEventString);
		BufProcessEventString=NULL;
	}

	if (BufThreadEventString)
	{
		ExFreePool(BufThreadEventString);
		BufThreadEventString=NULL;
	}

	if (reg)
	{
		ZwClose(reg); 
		reg=0;
	}

	

	//fetch cpu info
	{
		DWORD r[4];
		DWORD a;

		__cpuid(r,0);
		DbgPrint("cpuid.0: r[1]=%x", r[1]);
		if (r[1]==0x756e6547) //GenuineIntel
		{

			__cpuid(r,1);

			a=r[0];
			
			cpu_stepping=a & 0xf;
			cpu_model=(a >> 4) & 0xf;
			cpu_familyID=(a >> 8) & 0xf;
			cpu_type=(a >> 12) & 0x3;
			cpu_ext_modelID=(a >> 16) & 0xf;
			cpu_ext_familyID=(a >> 20) & 0xff;

			cpu_model=cpu_model + (cpu_ext_modelID << 4);
			cpu_familyID=cpu_familyID + (cpu_ext_familyID << 4);

			if ((r[2]<<9) & 1)
			{
				DbgPrint("Intel cpu. IA32_FEATURE_CONTROL MSR=%x", readMSR(0x3a));		
			}
			else
			{
				DbgPrint("Intel cpu without IA32_FEATURE_CONTROL MSR");		
			}

			vmx_init_dovmcall(1);
			setup_APIC_BASE(); //for ultimap

		}
		else
		{
			DbgPrint("Not an intel cpu");
			if (r[1]==0x68747541)
			{
				DbgPrint("This is an AMD\n");
				vmx_init_dovmcall(0);
			}

		}



	}

	{
		APIC y;
		
		DebugStackState x;
		DbgPrint("offset of LBR_Count=%d\n", (UINT_PTR)&x.LBR_Count-(UINT_PTR)&x);

		DbgPrint("Testing forEachCpu(...)\n");
		forEachCpu(TestDPC, NULL, NULL, NULL);
		forEachCpuAsync(TestDPC, NULL, NULL, NULL);

		forEachCpuPassive(TestPassive, 0);

		DbgPrint("LVT_Performance_Monitor=%x\n", (UINT_PTR)&y.LVT_Performance_Monitor-(UINT_PTR)&y);
	}

	DbgPrint("No exceptions test:");
	if (NoExceptions_Enter())
	{
		int o = 45678;
		int x=0, r=0;
		//r=NoExceptions_CopyMemory(&x, &o, sizeof(x));

		r = NoExceptions_CopyMemory(&x, (PVOID)0, sizeof(x));

		DbgPrint("o=%d x=%d r=%d", o, x, r);


		DbgPrint("Leaving NoExceptions mode");
		NoExceptions_Leave();
	}


	RtlInitUnicodeString(&temp, L"PsSuspendProcess");
	PsSuspendProcess = MmGetSystemRoutineAddress(&temp);

	RtlInitUnicodeString(&temp, L"PsResumeProcess");
	PsResumeProcess = MmGetSystemRoutineAddress(&temp);

	// ============================================================
	// 初始化Socket通信（唯一的通信方式）
	// ============================================================
	DbgPrint("==========================================================\n");
	DbgPrint("Initializing Socket Communication (Primary Communication)\n");
	DbgPrint("==========================================================\n");
	
	ntStatus = SocketComm_Initialize();
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrint("[FATAL] Failed to initialize socket communication: 0x%X\n", ntStatus);
		DbgPrint("Driver cannot function without socket communication!\n");
		return ntStatus;
	}
	
	DbgPrint("[OK] Socket Communication initialized successfully\n");
	
	// 启动Socket监听
	ntStatus = SocketComm_StartListening();
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrint("[FATAL] Failed to start socket listener: 0x%X\n", ntStatus);
		DbgPrint("Driver cannot function without socket listener!\n");
		SocketComm_Cleanup();
		return ntStatus;
	}
	
	DbgPrint("[OK] Socket listener started on port %d\n", SOCKET_SERVER_PORT);
	DbgPrint("==========================================================\n");
	DbgPrint("Driver loaded successfully - Socket communication ready\n");
	DbgPrint("Connect to: 127.0.0.1:%d\n", SOCKET_SERVER_PORT);
	DbgPrint("==========================================================\n");
	
    return STATUS_SUCCESS;
}



// ============================================================
// 注意：以下IRP处理函数已被移除，因为不再使用设备对象
// 所有通信都通过Socket完成
// ============================================================
// 
// 原有的 DispatchCreate 和 DispatchClose 函数已移除
// 权限检查现在在Socket连接时进行
//
// ============================================================


typedef NTSTATUS (*PSRCTNR)(__in PCREATE_THREAD_NOTIFY_ROUTINE NotifyRoutine);
PSRCTNR PsRemoveCreateThreadNotifyRoutine2;

typedef NTSTATUS (*PSRLINR)(__in PLOAD_IMAGE_NOTIFY_ROUTINE NotifyRoutine);
PSRLINR PsRemoveLoadImageNotifyRoutine2;



void UnloadDriver(PDRIVER_OBJECT DriverObject)
{
	DbgPrint("==========================================================\n");
	DbgPrint("Unloading DBK Driver (Socket Communication Mode)\n");
	DbgPrint("==========================================================\n");
	
	if (!debugger_stopDebugging())
	{
		DbgPrint("Can not unload the driver because of debugger\n");
		return;
	}

	// 首先清理Socket通信（最重要）
	DbgPrint("[1/5] Cleaning up Socket Communication...\n");
	SocketComm_Cleanup();
	DbgPrint("[OK] Socket Communication cleaned up\n");

	DbgPrint("[2/5] Disabling Ultimap...\n");
	ultimap_disable();
	DisableUltimap2();
	DbgPrint("[OK] Ultimap disabled\n");

	DbgPrint("[3/5] Cleaning APIC...\n");
	clean_APIC_BASE();
	DbgPrint("[OK] APIC cleaned\n");

	DbgPrint("[4/5] Cleaning up NoExceptions...\n");
	NoExceptions_Cleanup();
	DbgPrint("[OK] NoExceptions cleaned up\n");
	

	if (KeServiceDescriptorTableShadow && registered) //I can't unload without a shadotw table (system service registered)
	{
		//1 since my routine finds the address of the 2nd element
		KeServiceDescriptorTableShadow[1].ArgumentTable=NULL;
		KeServiceDescriptorTableShadow[1].CounterTable=NULL;
		KeServiceDescriptorTableShadow[1].ServiceTable=NULL;
		KeServiceDescriptorTableShadow[1].TableSize=0;

		KeServiceDescriptorTable[2].ArgumentTable=NULL;
		KeServiceDescriptorTable[2].CounterTable=NULL;
		KeServiceDescriptorTable[2].ServiceTable=NULL;
		KeServiceDescriptorTable[2].TableSize=0;
	}
		

	if ((CreateProcessNotifyRoutineEnabled) || (ImageNotifyRoutineLoaded)) 
	{
		PVOID x;
		UNICODE_STRING temp;

		RtlInitUnicodeString(&temp, L"PsRemoveCreateThreadNotifyRoutine");
		PsRemoveCreateThreadNotifyRoutine2=MmGetSystemRoutineAddress(&temp);

		RtlInitUnicodeString(&temp, L"PsRemoveCreateThreadNotifyRoutine");
		PsRemoveLoadImageNotifyRoutine2=MmGetSystemRoutineAddress(&temp);
		
		RtlInitUnicodeString(&temp, L"ObOpenObjectByName");
		x=MmGetSystemRoutineAddress(&temp);
		
		DbgPrint("ObOpenObjectByName=%p\n",x);
			

		if ((PsRemoveCreateThreadNotifyRoutine2) && (PsRemoveLoadImageNotifyRoutine2))
		{
			DbgPrint("Stopping processwatch\n");

			if (CreateProcessNotifyRoutineEnabled)
			{
				DbgPrint("Removing process watch");
#if (NTDDI_VERSION >= NTDDI_VISTASP1)
				PsSetCreateProcessNotifyRoutineEx(CreateProcessNotifyRoutineEx,TRUE);
#else
				PsSetCreateProcessNotifyRoutine(CreateProcessNotifyRoutine,TRUE);
#endif

				
				DbgPrint("Removing thread watch");
				PsRemoveCreateThreadNotifyRoutine2(CreateThreadNotifyRoutine);
			}

			if (ImageNotifyRoutineLoaded)
				PsRemoveLoadImageNotifyRoutine2(LoadImageNotifyRoutine);
		}
		else return;  //leave now!!!!!		
	}


	DbgPrint("[5/5] Final cleanup...\n");

	// 注意：不再删除设备对象，因为我们没有创建设备对象
	// 所有通信都通过Socket完成

#ifdef CETC
#ifndef CETC_RELEASE
	UnloadCETC(); //not possible in the final build
#endif
#endif

	CleanProcessList();

	ExDeleteResourceLite(&ProcesslistR);

	RtlZeroMemory(&ProcesslistR, sizeof(ProcesslistR));

#if (NTDDI_VERSION >= NTDDI_VISTA)
	if (DRMHandle)
	{
		DbgPrint("Unregistering DRM handle\n");
		ObUnRegisterCallbacks(DRMHandle);
		DRMHandle = NULL;
	}
#endif

	DbgPrint("[OK] Final cleanup completed\n");
	DbgPrint("==========================================================\n");
	DbgPrint("DBK Driver unloaded successfully\n");
	DbgPrint("==========================================================\n");
}