#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <algorithm>

// 콘솔 줄 출력 (콘솔 너비만큼 공백 패딩하여 잔상 방지)
inline void ConsolePrintLine(const wchar_t* fmt, ...) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int width = csbi.dwSize.X;

    wchar_t buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vswprintf(buf, 256, fmt, args);
    va_end(args);
    if (len < 0) len = 0;

    // 콘솔 너비까지 공백 패딩 + 개행
    int padEnd = (std::min)(width - 1, 255);
    for (int i = len; i < padEnd; i++)
        buf[i] = L' ';
    buf[padEnd] = L'\n';
    int total = padEnd + 1;
    buf[total] = L'\0';

    DWORD written;
    WriteConsole(hConsole, buf, total, &written, NULL);
}
