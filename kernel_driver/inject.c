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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "driver.h"

// ---------------------------------------------------------------------------
// Injection: while attached to the target, write the DLL path into its
// address space and create a real user thread inside the target whose start
// routine is the target's kernel32!LoadLibraryW. Because the thread is born
// in the target process, LoadLibraryW runs with the target's own loader - no
// manual PE fixing is needed inside the target, and no APC machinery.
// ---------------------------------------------------------------------------

// ntddk.h does not declare ZwCreateThreadEx (it lives in ntifs.h); declare it
// ourselves. The attribute list is always NULL here.
NTKERNELAPI NTSTATUS NTAPI ZwCreateThreadEx(PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess,
                                            POBJECT_ATTRIBUTES ObjectAttributes,
                                            HANDLE ProcessHandle, PVOID StartRoutine,
                                            PVOID Argument, ULONG CreateFlags, SIZE_T ZeroBits,
                                            SIZE_T StackSize, SIZE_T MaximumStackSize,
                                            PVOID AttributeList);

// Public headers don't expose LDR_DATA_TABLE_ENTRY; this is the stable x64
// prefix used for the InLoadOrderModuleList walk.
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

// No CRT: hand-rolled equivalents of the two string helpers used here.
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

// Resolves an export of a module mapped in the current (attached) address
// space. Reads are guarded with SEH because the loader structures can change
// underneath us.
static PVOID RdiFindExport(PVOID ModuleBase, PCSTR ExportName)
{
  PVOID result = NULL;

  __try
  {
    PIMAGE_NT_HEADERS nt = RtlImageNtHeader(ModuleBase);
    if(nt == NULL)
      return NULL;

    DWORD expRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if(expRva == 0)
      return NULL;

    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((PCHAR)ModuleBase + expRva);

    DWORD *names = (DWORD *)((PCHAR)ModuleBase + exp->AddressOfNames);
    WORD *ordinals = (WORD *)((PCHAR)ModuleBase + exp->AddressOfNameOrdinals);
    DWORD *functions = (DWORD *)((PCHAR)ModuleBase + exp->AddressOfFunctions);

    for(DWORD i = 0; i < exp->NumberOfNames; i++)
    {
      PCHAR name = (PCHAR)ModuleBase + names[i];
      if(RdiStrcmp(name, ExportName) == 0)
      {
        DWORD funcRva = functions[ordinals[i]];
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

// Finds kernel32!ExportName in the target process. Must be called while
// attached to the target (the PEB and module list are read from the attached
// address space).
static PVOID RdiResolveKernel32Export(PEPROCESS TargetProcess, PCSTR ExportName)
{
  PPEB peb = PsGetProcessPeb(TargetProcess);
  if(peb == NULL)
    return NULL;

  PVOID result = NULL;

  __try
  {
    PEB_LDR_DATA *ldr = peb->Ldr;
    if(ldr == NULL)
      return NULL;

    LIST_ENTRY *head = &ldr->InLoadOrderModuleList;
    for(LIST_ENTRY *entry = head->Flink; entry != head; entry = entry->Flink)
    {
      RDI_LDR_ENTRY *mod = CONTAINING_RECORD(entry, RDI_LDR_ENTRY, InLoadOrderLinks);

      UNICODE_STRING kernel32Name = RTL_CONSTANT_STRING(L"kernel32.dll");
      if(RtlEqualUnicodeString(&mod->BaseDllName, &kernel32Name, TRUE))
      {
        result = RdiFindExport(mod->DllBase, ExportName);
        break;
      }
    }
  }
  __except(EXCEPTION_EXECUTE_HANDLER)
  {
    result = NULL;
  }

  return result;
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
    // 1. Write the DLL path into the target's address space.
    SIZE_T pathBytes = (RdiWcslen(DllPath) + 1) * sizeof(WCHAR);

    PVOID remotePath = NULL;
    SIZE_T regionSize = pathBytes;
    status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &remotePath, 0, &regionSize,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if(!NT_SUCCESS(status) || remotePath == NULL)
      break;

    RtlCopyMemory(remotePath, DllPath, pathBytes);

    // 2. Resolve LoadLibraryW in the target's kernel32.
    PVOID loadLibraryW = RdiResolveKernel32Export(target, "LoadLibraryW");
    if(loadLibraryW == NULL)
    {
      status = STATUS_PROCEDURE_NOT_FOUND;
      break;
    }

    // 3. Create a real user thread inside the target. ZwCreateThreadEx is
    // called while attached, so ZwCurrentProcess() resolves to the target
    // process and the new thread runs LoadLibraryW(remotePath) in the
    // target's address space.
    HANDLE threadHandle = NULL;
    status = ZwCreateThreadEx(&threadHandle, THREAD_ALL_ACCESS, NULL, ZwCurrentProcess(),
                              loadLibraryW, remotePath, 0, NULL, 0, 0, NULL);
    if(!NT_SUCCESS(status) || threadHandle == NULL)
      break;

    ZwClose(threadHandle);

    // The thread runs LoadLibraryW(remotePath) in the target and exits on its
    // own. The path allocation is intentionally leaked: it is referenced by
    // the running thread and cannot be freed safely from here.
    status = STATUS_SUCCESS;
  } while(FALSE);

  KeUnstackDetachProcess(&apcState);
  ObDereferenceObject(target);

  return status;
}
