#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>

#include <atomic>
#include <cstdio>

#pragma comment(lib, "Dbghelp.lib")

// Produces a local minidump only. External notifications are deliberately
// excluded so the public repository never contains a webhook or destination.
class CrashDump {
public:
    using CrashInfoCallback = void (*)(char* outBuf, int bufSize);

    CrashDump() {
        CreateDirectoryW(L"Dumps", nullptr);
        SetUnhandledExceptionFilter(&UnhandledFilter);
    }

    static void SetCrashInfoCallback(CrashInfoCallback callback) {
        sInfoCallback = callback;
    }

    static void WriteShutdownDump(const wchar_t* reason) {
        WriteDump(nullptr, reason);
    }

    static void WriteMiniDumpNowOnce(const wchar_t* reason) {
        if (!sOnce.test_and_set(std::memory_order_acq_rel)) WriteDump(nullptr, reason);
    }

private:
    static LONG WINAPI UnhandledFilter(PEXCEPTION_POINTERS exceptionInfo) {
        WriteDump(exceptionInfo, L"unhandled exception");
        return EXCEPTION_EXECUTE_HANDLER;
    }

    static void WriteDump(PEXCEPTION_POINTERS exceptionInfo, const wchar_t* reason) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t path[MAX_PATH]{};
        _snwprintf_s(path, _countof(path), _TRUNCATE,
            L"Dumps\\server-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            GetCurrentProcessId());

        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;

        MINIDUMP_EXCEPTION_INFORMATION dumpException{};
        dumpException.ThreadId = GetCurrentThreadId();
        dumpException.ExceptionPointers = exceptionInfo;
        dumpException.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            MiniDumpWithThreadInfo, exceptionInfo ? &dumpException : nullptr, nullptr, nullptr);
        CloseHandle(file);

        if (reason != nullptr) std::wprintf(L"Dump written: %ls (%ls)\n", path, reason);
    }

    inline static CrashInfoCallback sInfoCallback = nullptr;
    inline static std::atomic_flag sOnce = ATOMIC_FLAG_INIT;
};
