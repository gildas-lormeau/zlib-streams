// win_compat.cpp
// Minimal Win32-like stubs to satisfy 7-Zip SDK symbols on non-Windows hosts.
// These are intentionally small and conservative: they provide just enough
// linkage so the SDK can be built on macOS. They do not fully implement
// Windows semantics; they're a pragmatic shim for the generator build.

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../7zip/CPP/Common/MyWindows.h"

extern "C" {
// Basic handle type and sentinel
typedef void *HANDLE;
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif
#include "compat_prefix.h"

// File mapping / mmap stubs
HANDLE CreateFileMapping(HANDLE /*hFile*/, void * /*lpAttributes*/,
                         DWORD /*flProtect*/, DWORD /*dwMaximumSizeHigh*/,
                         DWORD /*dwMaximumSizeLow*/, const char * /*lpName*/) {
  // Indicate no mapping by returning INVALID_HANDLE_VALUE
  return INVALID_HANDLE_VALUE;
}

HANDLE OpenFileMapping(DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/,
                       const char * /*lpName*/) {
  return INVALID_HANDLE_VALUE;
}

void *MapViewOfFile(HANDLE /*hFileMappingObject*/, DWORD /*dwDesiredAccess*/,
                    DWORD /*dwFileOffsetHigh*/, DWORD /*dwFileOffsetLow*/,
                    size_t /*dwNumberOfBytesToMap*/) {
  return NULL;
}

void *MapViewOfFileEx(HANDLE /*hFileMappingObject*/, DWORD /*dwDesiredAccess*/,
                      DWORD /*dwFileOffsetHigh*/, DWORD /*dwFileOffsetLow*/,
                      size_t /*dwNumberOfBytesToMap*/,
                      void * /*lpBaseAddress*/) {
  return NULL;
}

BOOL UnmapViewOfFile(void * /*lpBaseAddress*/) { return TRUE; }

// Global memory implementations using malloc as a backend for HGLOBAL
HGLOBAL GlobalAlloc(UINT /*uFlags*/, SIZE_T dwBytes) {
  void *p = malloc(dwBytes ? dwBytes : 1);
  return (HGLOBAL)p;
}

HGLOBAL GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT /*uFlags*/) {
  void *p = realloc(hMem, dwBytes ? dwBytes : 1);
  return (HGLOBAL)p;
}

HGLOBAL GlobalFree(HGLOBAL hMem) {
  if (!hMem)
    return NULL;
  free(hMem);
  return NULL;
}

LPVOID GlobalLock(HGLOBAL hMem) { return hMem; }
BOOL GlobalUnlock(HGLOBAL /*hMem*/) { return TRUE; }

// Simple module loader stubs: treat modules as opaque non-NULL handles
HMODULE LoadLibraryA(const char * /*pszFileName*/) {
  return (HMODULE)0x1; // non-NULL
}
HMODULE LoadLibraryW(const wchar_t * /*pszFileName*/) { return (HMODULE)0x1; }
HMODULE LoadLibrary(const char *pszFileName) {
  return LoadLibraryA(pszFileName);
}
BOOL FreeLibrary(HMODULE /*hModule*/) { return TRUE; }
HMODULE GetModuleHandleW(const wchar_t * /*lpModuleName*/) {
  return (HMODULE)0x1;
}

// Minimal OpenProcessToken / LookupPrivilegeValue / AdjustTokenPrivileges stubs
BOOL OpenProcessToken(HANDLE /*ProcessHandle*/, DWORD /*DesiredAccess*/,
                      HANDLE *TokenHandle) {
  if (TokenHandle)
    *TokenHandle = (HANDLE)0x1;
  return TRUE;
}

BOOL LookupPrivilegeValueA(const char * /*lpSystemName*/,
                           const char * /*lpName*/, LUID *lpLuid) {
  if (lpLuid) {
    lpLuid->LowPart = 0;
    lpLuid->HighPart = 0;
  }
  return TRUE;
}

BOOL AdjustTokenPrivileges(HANDLE /*TokenHandle*/,
                           BOOL /*DisableAllPrivileges*/,
                           PTOKEN_PRIVILEGES /*NewState*/,
                           DWORD /*BufferLength*/,
                           PTOKEN_PRIVILEGES /*PreviousState*/,
                           DWORD * /*ReturnLength*/) {
  return TRUE;
}

// RtlGetVersion: populate OSVERSIONINFOEXW with macOS-like values but
// Windows-compatible fields
void RtlGetVersion(OSVERSIONINFOEXW *vi) {
  if (!vi)
    return;
  vi->dwOSVersionInfoSize = sizeof(*vi);
  vi->dwMajorVersion = 10;
  vi->dwMinorVersion = 0;
  vi->dwBuildNumber = 19041; // pretend modern Windows 10 build
  vi->dwPlatformId = VER_PLATFORM_WIN32_NT;
}

// Provide GetProcAddress and GetCurrentProcess stubs
FARPROC GetProcAddress(HMODULE /*hModule*/, const char * /*lpProcName*/) {
  return (FARPROC)0x1; // non-NULL pointer
}

HANDLE GetCurrentProcess() { return (HANDLE)0x1; }
}
