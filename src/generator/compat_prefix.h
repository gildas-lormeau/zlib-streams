// compat_prefix.h
// Pre-included header to provide minimal Windows types and function
// declarations so the 7-Zip SDK headers can be parsed on non-Windows hosts.

#ifndef INFLATE64_COMPAT_PREFIX_H
#define INFLATE64_COMPAT_PREFIX_H

#include <stdint.h>
#include <stddef.h>

// Calling convention macro for Windows APIs; on POSIX it's a no-op
#ifndef WINAPI
#define WINAPI
#endif

// Basic HANDLE and sentinel
typedef void *HANDLE;
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

// Minimal DWORD/BOOL types
typedef unsigned int DWORD;
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
// Common Windows integer typedefs
typedef unsigned int UINT;
typedef size_t SIZE_T;

// HMODULE / HGLOBAL / LPVOID aliases
typedef void *HMODULE;
typedef void *HGLOBAL;
typedef void *LPVOID;

// TCHAR / WCHAR helpers (we'll treat wide strings as wchar_t on POSIX)
typedef wchar_t WCHAR;
typedef const WCHAR *LPCWSTR;
#ifndef LPCTSTR
typedef const char *LPCTSTR;
#endif

// Pointer typedefs used by some SDK headers
typedef HANDLE *PHANDLE;
typedef struct _LUID *PLUID;
typedef DWORD *PDWORD;

// (FARPROC/GetProcAddress/GetCurrentProcess/LoadLibrary declarations are
// provided inside the extern "C" block below to ensure C linkage.)

// TEXT macro (narrow string pass-through for this shim)
#ifndef TEXT
#define TEXT(x) x
#endif

// Common privilege names used by the SDK
#ifndef SE_LOCK_MEMORY_NAME
#define SE_LOCK_MEMORY_NAME "SeLockMemoryPrivilege"
#endif
#ifndef SE_RESTORE_NAME
#define SE_RESTORE_NAME "SeRestorePrivilege"
#endif
// Basic Win32 API stubs used at compile time; real implementations are
// provided in src/generator/win_compat.cpp
#ifdef __cplusplus
extern "C" {
#endif
unsigned int GetLastError();
void SetLastError(unsigned int);
unsigned int GetCurrentThreadId();
unsigned int GetCurrentProcessId();
BOOL CloseHandle(HANDLE h);
HANDLE CreateFileMapping(HANDLE hFile, void *lpAttributes, DWORD flProtect,
                         DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow,
                         const char *lpName);
HANDLE OpenFileMapping(DWORD dwDesiredAccess, BOOL bInheritHandle,
                       const char *lpName);
void *MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess,
                    DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow,
                    size_t dwNumberOfBytesToMap);
void *MapViewOfFileEx(HANDLE hFileMappingObject, DWORD dwDesiredAccess,
                      DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow,
                      size_t dwNumberOfBytesToMap, void *lpBaseAddress);
BOOL UnmapViewOfFile(void *lpBaseAddress);

// FARPROC and GetProcAddress
typedef void *FARPROC;
FARPROC GetProcAddress(HMODULE hModule, const char *lpProcName);

// Current process helper
HANDLE GetCurrentProcess();

// Provide LoadLibrary forward declarations matching Windows APIs
HMODULE LoadLibraryA(const char *pszFileName);
HMODULE LoadLibraryW(const WCHAR *pszFileName);

// Global memory APIs
HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
HGLOBAL GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT uFlags);
HGLOBAL GlobalFree(HGLOBAL hMem);
LPVOID GlobalLock(HGLOBAL hMem);
BOOL GlobalUnlock(HGLOBAL hMem);

// Module / library helpers
HMODULE LoadLibraryA(const char *pszFileName);
HMODULE LoadLibraryW(const WCHAR *pszFileName);
HMODULE LoadLibrary(const char *pszFileName);
BOOL FreeLibrary(HMODULE hModule);
HMODULE GetModuleHandleW(LPCWSTR lpModuleName);

// Version information
typedef struct _OSVERSIONINFOEXW {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
} OSVERSIONINFOEXW;

// Token / privilege minimal types used by MemoryLock.cpp
typedef struct _LUID {
  DWORD LowPart;
  long HighPart;
} LUID;
typedef struct _LUID_AND_ATTRIBUTES {
  LUID Luid;
  DWORD Attributes;
} LUID_AND_ATTRIBUTES;
typedef struct _TOKEN_PRIVILEGES {
  DWORD PrivilegeCount;
  LUID_AND_ATTRIBUTES Privileges[1];
} TOKEN_PRIVILEGES, *PTOKEN_PRIVILEGES;

// Token API stubs
BOOL OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess,
                      HANDLE *TokenHandle);
BOOL LookupPrivilegeValueA(const char *lpSystemName, const char *lpName,
                           LUID *lpLuid);
BOOL AdjustTokenPrivileges(HANDLE TokenHandle, BOOL DisableAllPrivileges,
                           PTOKEN_PRIVILEGES NewState, DWORD BufferLength,
                           PTOKEN_PRIVILEGES PreviousState,
                           DWORD *ReturnLength);

// RtlGetVersion stub declared here so implementations can be provided in
// win_compat.cpp
typedef void(WINAPI *Func_RtlGetVersion)(OSVERSIONINFOEXW *);

#define TOKEN_ADJUST_PRIVILEGES 0x0020
#define SE_PRIVILEGE_ENABLED 0x00000002
#define ERROR_SUCCESS 0
#define VER_PLATFORM_WIN32_NT 2
// (WINAPI already defined above.)

// Global memory flags
#ifndef GMEM_MOVEABLE
#define GMEM_MOVEABLE 0x0002
#endif

#ifdef __cplusplus
}
#endif

#endif // INFLATE64_COMPAT_PREFIX_H
