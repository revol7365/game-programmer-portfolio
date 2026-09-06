#pragma once

#include <windows.h>
#include <fstream>
#include <string>
#include <iostream>

// 1. 클래스 이름을 CLog로 변경하여 매크로와 겹치지 않게 합니다.
class CLog
{
private:
    CRITICAL_SECTION _cs;
    std::wofstream _logFile;
    unsigned int _writeCount = 0;

    CLog() {
        std::wcout.imbue(std::locale("korean"));

        InitializeCriticalSection(&_cs);
        CreateDirectory(L"Logs", NULL);

        SYSTEMTIME st;
        GetLocalTime(&st);
        WCHAR logName[MAX_PATH];
        swprintf_s(logName, L"Logs\\Log_%04d%02d%02d_%02d%02d%02d.log",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        _logFile.imbue(std::locale("korean"));
        _logFile.open(logName, std::ios::app);
    }

public:
    static CLog& GetInstance() {
        static CLog instance;
        return instance;
    }

    ~CLog() {
        EnterCriticalSection(&_cs);
        if (_logFile.is_open()) _logFile.close();
        LeaveCriticalSection(&_cs);
        DeleteCriticalSection(&_cs);
    }

    void Write(const wchar_t* format, ...) {
        WCHAR buffer[2048]; // 버퍼를 넉넉하게 잡습니다.
        va_list args;
        va_start(args, format);

        // vswprintf_s를 사용하여 안전하게 문자열 조립
        int result = vswprintf_s(buffer, 2048, format, args);
        va_end(args);

        // 포맷팅 실패 원인을 파악하기 위해 콘솔에 출력
        if (result == -1) {
            std::wcout << L"[LogSystem Error] Formatting failed. Format string: " << format << std::endl;
            return;
        }

        std::wstring message = buffer;

        // 콘솔 및 파일 출력 로직 (기존과 동일)
        // std::wcout << message << std::endl;

        EnterCriticalSection(&_cs);
        if (_logFile.is_open()) {

            if (_logFile.fail()) {
                _logFile.clear();
            }

            SYSTEMTIME st;
            GetLocalTime(&st);
            WCHAR timeBuf[32];
            swprintf_s(timeBuf, 32, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
            // std::endl / flush() 는 메시지당 디스크 sync → 대량 이벤트 시 워커 직렬화로
            // 세션 cleanup 지연·로그인 throughput 저하 유발. "\n" 만 쓰고 OS 버퍼링에 맡김.
            _logFile << timeBuf << message << L"\n";
            // 100 메시지마다 강제 flush — 비정상 종료 시 손실 최소화
            if ((++_writeCount % 100) == 0) _logFile.flush();
        }
        LeaveCriticalSection(&_cs);
    }
};

// 2. 매크로 이름은 LOG, 호출하는 클래스는 CLog로 명확히 구분합니다.
#define LOG(fmt, ...) CLog::GetInstance().Write(fmt, ##__VA_ARGS__)
//#define LOG(fmt, ...) ""