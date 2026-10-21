#include "platform/CrashHandler.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cwchar>

namespace abyss {

namespace {

wchar_t g_dumpDirectory[MAX_PATH]{};

LONG WINAPI writeMinidump(EXCEPTION_POINTERS *exception)
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t path[MAX_PATH]{};
    std::swprintf(path, MAX_PATH, L"%s\\AbyssStream-crash-%04u%02u%02u-%02u%02u%02u.dmp", g_dumpDirectory, now.wYear,
                  now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers = FALSE;
        // MiniDumpNormal: thread stacks and loaded modules, not the whole heap.
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH; // let Windows Error Reporting run as usual
}

} // namespace

void installCrashHandler(const std::wstring &dumpDirectory)
{
    wcsncpy_s(g_dumpDirectory, dumpDirectory.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(&writeMinidump);
}

} // namespace abyss

#else

namespace abyss {
void installCrashHandler(const std::wstring &) {}
} // namespace abyss

#endif
