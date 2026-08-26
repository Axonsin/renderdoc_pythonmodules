/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2014-2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ******************************************************************************/

// ntifs.h must come first: it defines _NTIFS_/_NTIFS_INCLUDED_ and then
// includes ntddk.h itself, so wdm.h skips its own PEPROCESS/PETHREAD
// typedefs. Including ntddk.h before ntifs.h redefines them with a
// different base type (C2371).
#include <ntifs.h>
#include "driver.h"
#include <ntimage.h>

// ---------------------------------------------------------------------------
// Injection: while attached to the target, write the DLL path into its
// address space and run the target's own kernel32!LoadLibraryW on that path.
// Both 64-bit and 32-bit (WOW64) targets are supported:
//   - primary: ZwCreateThreadEx creates a thread whose start routine is the
//     target's (possibly 32-bit) LoadLibraryW; a WOW64 process routes new
//     threads through the wow64 bootstrap so a 32-bit start address runs in
//     32-bit mode.
//   - fallback (thread creation failed): hijack an existing thread's context
//     into LoadLibraryW and restore it once the module is loaded
//     (RdiHijackInject below).
// ---------------------------------------------------------------------------

// ntoskrnl exports without public declarations in the WDK headers (all are
// present in ntoskrnl.lib - verified with dumpbin).
NTKERNELAPI PIMAGE_NT_HEADERS RtlImageNtHeader(PVOID BaseAddress);
NTKERNELAPI PPEB PsGetProcessPeb(PEPROCESS Process);
// PEB32 address for WOW64 processes, NULL for native 64-bit ones.
NTKERNELAPI PVOID PsGetProcessWow64Process(PEPROCESS Process);
NTKERNELAPI NTSTATUS PsSuspendProcess(PEPROCESS Process);
NTKERNELAPI NTSTATUS PsResumeProcess(PEPROCESS Process);
NTKERNELAPI NTSTATUS PsGetContextThread(PETHREAD Thread, PCONTEXT ThreadContext);
NTKERNELAPI NTSTATUS PsSetContextThread(PETHREAD Thread, PCONTEXT ThreadContext);
NTKERNELAPI NTSTATUS NTAPI ZwQuerySystemInformation(ULONG SystemInformationClass,
                                                    PVOID SystemInformation,
                                                    ULONG SystemInformationLength,
                                                    PULONG ReturnLength);

// ZwCreateThreadEx is not declared in any public WDK header and not present
// in ntoskrnl.lib (undocumented export), so it is resolved at runtime below.
typedef NTSTATUS(NTAPI *RDI_CREATE_THREAD_EX)(PHANDLE ThreadHandle,
                                              ACCESS_MASK DesiredAccess,
                                              POBJECT_ATTRIBUTES ObjectAttributes,
                                              HANDLE ProcessHandle, PVOID StartRoutine,
                                              PVOID Argument, ULONG CreateFlags, SIZE_T ZeroBits,
                                              SIZE_T StackSize, SIZE_T MaximumStackSize,
                                              PVOID AttributeList);

// Minimal kernel-side definitions of the user-mode PEB / PEB_LDR_DATA (the
// WDK headers only carry an opaque `struct _PEB *`). Offsets are stable on
// x64: Ldr sits at PEB+0x18, the load-order list head at Ldr+0x10.
typedef struct _RDI_PEB_LDR_DATA
{
  ULONG Length;                                   // +0x00
  UCHAR Initialized;                              // +0x04
  PVOID SsHandle;                                 // +0x08
  LIST_ENTRY InLoadOrderModuleList;               // +0x10
  LIST_ENTRY InMemoryOrderModuleList;             // +0x20
  LIST_ENTRY InInitializationOrderModuleList;     // +0x30
} RDI_PEB_LDR_DATA;

typedef struct _RDI_PEB
{
  UCHAR InheritedAddressSpacePermission;    // +0x00
  UCHAR ReadImageFileExecOptions;           // +0x01
  UCHAR BeingDebugged;                      // +0x02
  UCHAR BitField;                           // +0x03
  PVOID Mutant;                             // +0x08
  PVOID ImageBaseAddress;                   // +0x10
  RDI_PEB_LDR_DATA *Ldr;                    // +0x18
} RDI_PEB;

C_ASSERT(FIELD_OFFSET(RDI_PEB, Ldr) == 0x18);
C_ASSERT(FIELD_OFFSET(RDI_PEB_LDR_DATA, InLoadOrderModuleList) == 0x10);

// Public headers don't expose LDR_DATA_TABLE_ENTRY; these are the stable
// prefixes used for the InLoadOrderModuleList walk (64-bit and 32-bit).
typedef struct _RDI_LDR_ENTRY
{
  LIST_ENTRY InLoadOrderLinks;              // +0x00
  LIST_ENTRY InMemoryOrderLinks;            // +0x10
  LIST_ENTRY InInitializationOrderLinks;    // +0x20
  PVOID DllBase;                            // +0x30
  PVOID EntryPoint;                         // +0x38
  ULONG SizeOfImage;                        // +0x40
  UNICODE_STRING FullDllName;               // +0x48
  UNICODE_STRING BaseDllName;               // +0x58
} RDI_LDR_ENTRY;

C_ASSERT(FIELD_OFFSET(RDI_LDR_ENTRY, DllBase) == 0x30);
C_ASSERT(FIELD_OFFSET(RDI_LDR_ENTRY, BaseDllName) == 0x58);

// 32-bit equivalents for WOW64 targets (walked through the PEB32 returned
// by PsGetProcessWow64Process). Offsets are stable on x86: Ldr sits at
// PEB32+0x0C, the load-order list head at Ldr32+0x0C.
typedef struct _RDI_LIST32
{
  ULONG Flink;
  ULONG Blink;
} RDI_LIST32;

typedef struct _RDI_UNICODE_STRING32
{
  USHORT Length;
  USHORT MaximumLength;
  ULONG Buffer;    // 32-bit user address
} RDI_UNICODE_STRING32;

typedef struct _RDI_PEB_LDR_DATA32
{
  ULONG Length;                                  // +0x00
  UCHAR Initialized;                             // +0x04
  ULONG SsHandle;                                // +0x08
  RDI_LIST32 InLoadOrderModuleList;              // +0x0C
  RDI_LIST32 InMemoryOrderModuleList;            // +0x14
  RDI_LIST32 InInitializationOrderModuleList;    // +0x1C
} RDI_PEB_LDR_DATA32;

typedef struct _RDI_PEB32
{
  UCHAR InheritedAddressSpacePermission;    // +0x00
  UCHAR ReadImageFileExecOptions;           // +0x01
  UCHAR BeingDebugged;                      // +0x02
  UCHAR BitField;                           // +0x03
  ULONG Mutant;                             // +0x04
  ULONG ImageBaseAddress;                   // +0x08
  ULONG Ldr;                                // +0x0C (RDI_PEB_LDR_DATA32*)
} RDI_PEB32;

typedef struct _RDI_LDR_ENTRY32
{
  RDI_LIST32 InLoadOrderLinks;              // +0x00
  RDI_LIST32 InMemoryOrderLinks;            // +0x08
  RDI_LIST32 InInitializationOrderLinks;    // +0x10
  ULONG DllBase;                            // +0x18
  ULONG EntryPoint;                         // +0x1C
  ULONG SizeOfImage;                        // +0x20
  RDI_UNICODE_STRING32 FullDllName;         // +0x24
  RDI_UNICODE_STRING32 BaseDllName;         // +0x2C
} RDI_LDR_ENTRY32;

C_ASSERT(FIELD_OFFSET(RDI_PEB32, Ldr) == 0x0C);
C_ASSERT(FIELD_OFFSET(RDI_PEB_LDR_DATA32, InLoadOrderModuleList) == 0x0C);
C_ASSERT(FIELD_OFFSET(RDI_LDR_ENTRY32, DllBase) == 0x18);
C_ASSERT(FIELD_OFFSET(RDI_LDR_ENTRY32, BaseDllName) == 0x2C);

// The WDK headers only declare an opaque `struct _CONTEXT *`; this is the
// full x64 CONTEXT (same layout as winnt.h) required by
// PsGet/PsSetContextThread. Only ContextFlags/SegCs/Rcx/Rsp/Rip are used.
#define RDI_CONTEXT_AMD64 0x00100000UL
#define RDI_CONTEXT_CONTROL (RDI_CONTEXT_AMD64 | 0x1UL)
#define RDI_CONTEXT_INTEGER (RDI_CONTEXT_AMD64 | 0x2UL)
#define RDI_CONTEXT_SEGMENTS (RDI_CONTEXT_AMD64 | 0x4UL)

typedef struct __declspec(align(16)) _RDI_CONTEXT
{
  ULONG64 P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;    // +0x00
  ULONG ContextFlags;                                         // +0x30
  ULONG MxCsr;                                                // +0x34
  USHORT SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;            // +0x38
  ULONG EFlags;                                               // +0x44
  ULONG64 Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;                       // +0x48
  ULONG64 Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi;             // +0x78
  ULONG64 R8, R9, R10, R11, R12, R13, R14, R15;               // +0xB8
  ULONG64 Rip;                                                // +0xF8
  ULONG64 FltSave[64];                                        // +0x100 (XSAVE_FORMAT incl. Reserved4[96])
  ULONG64 VectorRegister[52];                                 // +0x300 (M128A[26])
  ULONG64 VectorControl;                                      // +0x4A0
  ULONG64 DebugControl;                                       // +0x4A8
  ULONG64 LastBranchToRip;                                    // +0x4B0
  ULONG64 LastBranchFromRip;                                  // +0x4B8
  ULONG64 LastExceptionToRip;                                 // +0x4C0
  ULONG64 LastExceptionFromRip;                               // +0x4C8
} RDI_CONTEXT, *PRDI_CONTEXT;

C_ASSERT(sizeof(RDI_CONTEXT) == 0x4D0);
C_ASSERT(FIELD_OFFSET(RDI_CONTEXT, ContextFlags) == 0x30);
C_ASSERT(FIELD_OFFSET(RDI_CONTEXT, SegCs) == 0x38);
C_ASSERT(FIELD_OFFSET(RDI_CONTEXT, Rcx) == 0x80);
C_ASSERT(FIELD_OFFSET(RDI_CONTEXT, Rsp) == 0x98);
C_ASSERT(FIELD_OFFSET(RDI_CONTEXT, Rip) == 0xF8);

// ZwQuerySystemInformation(SystemProcessInformation) walk offsets (x64):
// the process header is 0x100 bytes, each SYSTEM_THREAD_INFORMATION 0x50.
#define RDI_SPI_NEXTENTRYOFFSET 0x00
#define RDI_SPI_THREADCOUNT 0x04
#define RDI_SPI_UNIQUEPROCESSID 0x50
#define RDI_SPI_THREAD_BASE 0x100
#define RDI_SPI_THREAD_STRIDE 0x50
#define RDI_SPI_THREAD_CLIENT_TID 0x30

// No CRT: hand-rolled equivalents of the string helpers used here.
static SIZE_T RdiWcslen(PCWSTR s)
{
  SIZE_T n = 0;
  while(s[n] != 0)
    n++;
  return n;
}

static int RdiStrcmp(PCSTR a, PCSTR b)
{
  while(*a != 0 && *a == *b)
  {
    a++;
    b++;
  }
  return (int)((unsigned char)*a - (unsigned char)*b);
}

static PCWSTR RdiWcsrchr(PCWSTR s, WCHAR c)
{
  PCWSTR last = NULL;
  while(*s != 0)
  {
    if(*s == c)
      last = s;
    s++;
  }
  return last;
}

// Resolves an export of a module mapped in the current (attached) address
// space, handling both PE32+ (native 64-bit) and PE32 (WOW64 target)
// images - the export data directory sits at a different offset inside the
// two optional header flavours. Reads are guarded with SEH because the
// loader structures can change underneath us.
static PVOID RdiFindExport(PVOID ModuleBase, PCSTR ExportName)
{
  PVOID result = NULL;

  __try
  {
    PIMAGE_NT_HEADERS nt = RtlImageNtHeader(ModuleBase);
    if(nt == NULL)
      return NULL;

    ULONG expRva = 0;
    if(nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
      PIMAGE_NT_HEADERS32 nt32 = (PIMAGE_NT_HEADERS32)nt;
      expRva = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    }
    else if(nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
      expRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    }
    else
    {
      return NULL;
    }

    if(expRva == 0)
      return NULL;

    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((PCHAR)ModuleBase + expRva);

    ULONG *names = (ULONG *)((PCHAR)ModuleBase + exp->AddressOfNames);
    USHORT *ordinals = (USHORT *)((PCHAR)ModuleBase + exp->AddressOfNameOrdinals);
    ULONG *functions = (ULONG *)((PCHAR)ModuleBase + exp->AddressOfFunctions);

    for(ULONG i = 0; i < exp->NumberOfNames; i++)
    {
      PCHAR name = (PCHAR)ModuleBase + names[i];
      if(RdiStrcmp(name, ExportName) == 0)
      {
        ULONG funcRva = functions[ordinals[i]];
        if(funcRva != 0)
        {
          result = (PCHAR)ModuleBase + funcRva;
          break;
        }
      }
    }
  }
  __except(EXCEPTION_EXECUTE_HANDLER)
  {
    result = NULL;
  }

  return result;
}

// Returns the base address of the module whose BaseDllName equals ModuleName
// in the attached process's loader list - the 32-bit list for WOW64 targets
// (via the PEB32 from PsGetProcessWow64Process) or the 64-bit one otherwise.
// Must be called while attached to the target; reads are SEH-guarded.
static PVOID RdiFindModuleByName(PEPROCESS TargetProcess, PCWSTR ModuleName)
{
  PVOID result = NULL;

  __try
  {
    PVOID peb32 = PsGetProcessWow64Process(TargetProcess);
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, ModuleName);

    // Guard against a corrupt/unlinking list walking off into the weeds.
    const ULONG kMaxModules = 512;
    ULONG steps = 0;

    if(peb32 != NULL)
    {
      ULONG ldr32 = ((RDI_PEB32 *)peb32)->Ldr;
      RDI_PEB_LDR_DATA32 *ldr = (RDI_PEB_LDR_DATA32 *)(ULONG_PTR)ldr32;
      if(ldr == NULL)
        return NULL;

      ULONG head = (ULONG)(ULONG_PTR)&ldr->InLoadOrderModuleList;
      ULONG link = ldr->InLoadOrderModuleList.Flink;
      while(link != 0 && link != head && steps++ < kMaxModules)
      {
        RDI_LDR_ENTRY32 *mod = (RDI_LDR_ENTRY32 *)(ULONG_PTR)link;

        UNICODE_STRING baseName;
        baseName.Length = mod->BaseDllName.Length;
        baseName.MaximumLength = mod->BaseDllName.MaximumLength;
        baseName.Buffer = (PWCH)(ULONG_PTR)mod->BaseDllName.Buffer;

        if(RtlEqualUnicodeString(&baseName, &name, TRUE))
        {
          result = (PVOID)(ULONG_PTR)mod->DllBase;
          break;
        }

        link = mod->InLoadOrderLinks.Flink;
      }
    }
    else
    {
      RDI_PEB *peb = (RDI_PEB *)PsGetProcessPeb(TargetProcess);
      if(peb == NULL)
        return NULL;

      RDI_PEB_LDR_DATA *ldr = peb->Ldr;
      if(ldr == NULL)
        return NULL;

      LIST_ENTRY *head = &ldr->InLoadOrderModuleList;
      for(LIST_ENTRY *entry = head->Flink; entry != head && steps++ < kMaxModules;
          entry = entry->Flink)
      {
        RDI_LDR_ENTRY *mod = CONTAINING_RECORD(entry, RDI_LDR_ENTRY, InLoadOrderLinks);

        if(RtlEqualUnicodeString(&mod->BaseDllName, &name, TRUE))
        {
          result = mod->DllBase;
          break;
        }
      }
    }
  }
  __except(EXCEPTION_EXECUTE_HANDLER)
  {
    result = NULL;
  }

  return result;
}

// Finds kernel32!ExportName in the target process (32-bit kernel32 for
// WOW64 targets). Must be called while attached to the target.
static PVOID RdiResolveKernel32Export(PEPROCESS TargetProcess, PCSTR ExportName)
{
  PVOID kernel32 = RdiFindModuleByName(TargetProcess, L"kernel32.dll");
  if(kernel32 == NULL)
    return NULL;

  return RdiFindExport(kernel32, ExportName);
}

// Fallback injection when ZwCreateThreadEx fails: hijack an existing thread
// of the target into LoadLibraryW(remotePath) and restore its context once
// the module appears in the loader list (or after a timeout).
//
// A 64-bit CONTEXT is used even for WOW64 threads: the 32<->64-bit context
// conversion lives in user-mode wow64.dll, but a thread suspended inside
// 32-bit code has a perfectly valid 64-bit context whose low halves of
// Rip/Rsp are the live 32-bit state and whose SegCs is a 32-bit code
// selector - so the same capture/set/restore works for both bitnesses.
//
// Known races of the classic hijack technique (accepted, see driver.h):
//  - after LoadLibraryW returns, the thread re-executes the few original
//    instructions between completion and the restore with a rolled-back
//    context (non-idempotent side effects in that window repeat once);
//  - if the load never completes (timeout), the thread is restored anyway
//    while it may still hold the loader lock.
static NTSTATUS RdiHijackInject(PEPROCESS TargetProcess, PVOID LoadLibraryW, PVOID RemotePath,
                                PCWSTR DllPath)
{
  NTSTATUS status = STATUS_UNSUCCESSFUL;
  PETHREAD thread = NULL;
  PRDI_CONTEXT savedCtx = NULL;
  PRDI_CONTEXT workCtx = NULL;
  PVOID spiBuffer = NULL;
  BOOLEAN processSuspended = FALSE;
  BOOLEAN hijackApplied = FALSE;

  // The module name to poll for: the file name part of the injected path.
  PCWSTR moduleName = DllPath;
  PCWSTR sep = RdiWcsrchr(DllPath, L'\\');
  if(sep != NULL && sep + 1 > moduleName)
    moduleName = sep + 1;
  sep = RdiWcsrchr(moduleName, L'/');
  if(sep != NULL)
    moduleName = sep + 1;

  const BOOLEAN targetIsWow64 = (PsGetProcessWow64Process(TargetProcess) != NULL);

  do
  {
    savedCtx = (PRDI_CONTEXT)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(RDI_CONTEXT), 'dRIR');
    workCtx = (PRDI_CONTEXT)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(RDI_CONTEXT), 'dRIR');
    if(savedCtx == NULL || workCtx == NULL)
    {
      status = STATUS_INSUFFICIENT_RESOURCES;
      break;
    }

    // 1. Enumerate the target's threads (SystemProcessInformation).
    ULONG spiLength = 64 * 1024;
    for(;;)
    {
      spiBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, spiLength, 'SRIR');
      if(spiBuffer == NULL)
      {
        status = STATUS_INSUFFICIENT_RESOURCES;
        break;
      }

      ULONG returned = 0;
      status = ZwQuerySystemInformation(5 /* SystemProcessInformation */, spiBuffer, spiLength,
                                        &returned);
      if(status == STATUS_SUCCESS)
        break;

      ExFreePoolWithTag(spiBuffer, 'SRIR');
      spiBuffer = NULL;

      if(status != STATUS_INFO_LENGTH_MISMATCH || spiLength >= 1024 * 1024)
        break;

      spiLength *= 2;
    }

    if(spiBuffer == NULL)
    {
      if(NT_SUCCESS(status))
        status = STATUS_UNSUCCESSFUL;
      break;
    }

    // 2. Freeze the process so the contexts we capture are stable.
    status = PsSuspendProcess(TargetProcess);
    if(!NT_SUCCESS(status))
      break;
    processSuspended = TRUE;

    // 3. Pick a thread: for WOW64 targets prefer one parked in 32-bit code
    // (SegCs != 0x33) so the 32-bit start address resumes in 32-bit mode.
    {
      const HANDLE targetPid = PsGetProcessId(TargetProcess);
      PCHAR procEntry = (PCHAR)spiBuffer;

      for(;;)
      {
        ULONG nextOffset = *(ULONG *)(procEntry + RDI_SPI_NEXTENTRYOFFSET);
        ULONG threadCount = *(ULONG *)(procEntry + RDI_SPI_THREADCOUNT);

        if(*(HANDLE *)(procEntry + RDI_SPI_UNIQUEPROCESSID) == targetPid)
        {
          if(threadCount > 1024)
            threadCount = 1024;

          for(ULONG i = 0; i < threadCount && thread == NULL; i++)
          {
            PCHAR th = procEntry + RDI_SPI_THREAD_BASE + i * RDI_SPI_THREAD_STRIDE;
            HANDLE tid = *(HANDLE *)(th + RDI_SPI_THREAD_CLIENT_TID);

            PETHREAD candidate = NULL;
            if(!NT_SUCCESS(PsLookupThreadByThreadId(tid, &candidate)))
              continue;

            RtlZeroMemory(workCtx, sizeof(RDI_CONTEXT));
            workCtx->ContextFlags = RDI_CONTEXT_CONTROL | RDI_CONTEXT_INTEGER | RDI_CONTEXT_SEGMENTS;

            if(NT_SUCCESS(PsGetContextThread(candidate, (PCONTEXT)workCtx)))
            {
              if(!targetIsWow64 || workCtx->SegCs != 0x33)
              {
                thread = candidate;
                break;
              }
            }

            ObDereferenceObject(candidate);
          }
          break;
        }

        if(nextOffset == 0)
          break;
        procEntry += nextOffset;
      }
    }

    if(thread == NULL)
    {
      status = STATUS_NOT_FOUND;
      break;
    }

    // 4. Build the hijacked context. Stack writes are SEH-guarded user
    // writes in the attached address space.
    RtlCopyMemory(savedCtx, workCtx, sizeof(RDI_CONTEXT));

    __try
    {
      if(targetIsWow64)
      {
        // x86 stdcall LoadLibraryW(LPCWSTR): at entry [esp]=return address
        // (the original EIP), [esp+4]=argument.
        ULONG64 esp = ((workCtx->Rsp - 0x80) & ~0xFULL) - 8;
        *(ULONG *)(ULONG_PTR)esp = (ULONG)savedCtx->Rip;
        *(ULONG *)(ULONG_PTR)(esp + 4) = (ULONG)(ULONG_PTR)RemotePath;
        workCtx->Rsp = esp;
        workCtx->Rip = (ULONG64)(ULONG_PTR)LoadLibraryW;
      }
      else
      {
        // x64: at entry [rsp]=return address, Rsp % 16 == 8, RCX=argument.
        ULONG64 rsp = ((workCtx->Rsp - 0x80) & ~0xFULL) - 8;
        *(ULONG64 *)(ULONG_PTR)rsp = savedCtx->Rip;
        workCtx->Rsp = rsp;
        workCtx->Rip = (ULONG64)(ULONG_PTR)LoadLibraryW;
        workCtx->Rcx = (ULONG64)(ULONG_PTR)RemotePath;
      }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
      status = STATUS_ACCESS_VIOLATION;
      break;
    }

    status = PsSetContextThread(thread, (PCONTEXT)workCtx);
    if(!NT_SUCCESS(status))
      break;
    hijackApplied = TRUE;

    PsResumeProcess(TargetProcess);
    processSuspended = FALSE;

    // 5. Poll for the module appearing in the target's loader list
    // (max 5s). Unlike the thread-creation path this also detects a failed
    // load (wrong path, blocked dll) instead of failing silently.
    status = STATUS_TIMEOUT;
    for(ULONG waited = 0; waited < 5000; waited += 50)
    {
      LARGE_INTEGER interval;
      interval.QuadPart = -50 * 1000 * 10;    // 50ms, relative
      KeDelayExecutionThread(KernelMode, FALSE, &interval);

      if(RdiFindModuleByName(TargetProcess, moduleName) != NULL)
      {
        status = STATUS_SUCCESS;
        break;
      }
    }

    // 6. Restore the original context and let the process run again. Even
    // after a timeout the restore happens (see the race notes above).
    PsSuspendProcess(TargetProcess);
    if(NT_SUCCESS(status) || hijackApplied)
    {
      NTSTATUS restoreStatus = PsSetContextThread(thread, (PCONTEXT)savedCtx);
      if(!NT_SUCCESS(restoreStatus))
        status = restoreStatus;
    }
    PsResumeProcess(TargetProcess);
    processSuspended = FALSE;
  } while(FALSE);

  // Best-effort cleanup: never leave the process suspended.
  if(processSuspended)
    PsResumeProcess(TargetProcess);

  if(thread != NULL)
    ObDereferenceObject(thread);

  if(spiBuffer != NULL)
    ExFreePoolWithTag(spiBuffer, 'SRIR');
  if(savedCtx != NULL)
    ExFreePoolWithTag(savedCtx, 'dRIR');
  if(workCtx != NULL)
    ExFreePoolWithTag(workCtx, 'dRIR');

  return status;
}

NTSTATUS RdiInjectDll(HANDLE ProcessId, PCWSTR DllPath)
{
  PEPROCESS target = NULL;
  NTSTATUS status = PsLookupProcessByProcessId(ProcessId, &target);
  if(!NT_SUCCESS(status))
    return status;

  KAPC_STATE apcState;
  KeStackAttachProcess(target, &apcState);

  do
  {
    // 1. Write the DLL path into the target's address space. WOW64 user
    // allocations land below 4GB, which is what the 32-bit LoadLibraryW
    // argument needs.
    SIZE_T pathBytes = (RdiWcslen(DllPath) + 1) * sizeof(WCHAR);

    PVOID remotePath = NULL;
    SIZE_T regionSize = pathBytes;
    status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remotePath, 0, &regionSize,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if(!NT_SUCCESS(status) || remotePath == NULL)
      break;

    RtlCopyMemory(remotePath, DllPath, pathBytes);

    // 2. Resolve LoadLibraryW in the target's kernel32 (the 32-bit one for
    // WOW64 targets).
    PVOID loadLibraryW = RdiResolveKernel32Export(target, "LoadLibraryW");
    if(loadLibraryW == NULL)
    {
      status = STATUS_PROCEDURE_NOT_FOUND;
      break;
    }

    // 3. Primary path: create a real user thread in the target. For WOW64
    // targets the (32-bit) start routine makes the wow64 bootstrap run the
    // thread in 32-bit mode; the thread exits when LoadLibraryW returns.
    UNICODE_STRING routineName = RTL_CONSTANT_STRING(L"ZwCreateThreadEx");
    RDI_CREATE_THREAD_EX createThreadEx =
        (RDI_CREATE_THREAD_EX)MmGetSystemRoutineAddress(&routineName);
    if(createThreadEx != NULL)
    {
      HANDLE threadHandle = NULL;
      status = createThreadEx(&threadHandle, THREAD_ALL_ACCESS, NULL, ZwCurrentProcess(),
                              loadLibraryW, remotePath, 0, 0, 0, 0, NULL);
      if(NT_SUCCESS(status) && threadHandle != NULL)
      {
        ZwClose(threadHandle);
        status = STATUS_SUCCESS;
        break;
      }

      if(NT_SUCCESS(status))
        status = STATUS_UNSUCCESSFUL;
    }
    else
    {
      status = STATUS_PROCEDURE_NOT_FOUND;
    }

    // 4. Fallback: hijack an existing thread's context into LoadLibraryW.
    // The path allocation stays leaked in both paths - the loader reads it
    // asynchronously.
    status = RdiHijackInject(target, loadLibraryW, remotePath, DllPath);
  } while(FALSE);

  KeUnstackDetachProcess(&apcState);
  ObDereferenceObject(target);

  return status;
}
