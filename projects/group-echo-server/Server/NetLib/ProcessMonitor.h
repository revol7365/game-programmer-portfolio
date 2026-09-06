#pragma once

#include <windows.h>
#include <pdh.h>
#include <vector>

#pragma comment(lib, "pdh.lib")

class ProcessMonitor
{
public:
    ProcessMonitor()
    {
        WCHAR path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);

        WCHAR* pName = wcsrchr(path, L'\\');
        pName = pName ? pName + 1 : path;

        WCHAR* pDot = wcsrchr(pName, L'.');
        if (pDot) *pDot = L'\0';

        PdhOpenQueryW(NULL, 0, &mQuery);

        WCHAR counter[256];

        swprintf_s(counter, L"\\Process(%s)\\Private Bytes", pName);
        PdhAddCounterW(mQuery, counter, 0, &mPrivateBytes);

        swprintf_s(counter, L"\\Process(%s)\\Working Set", pName);
        PdhAddCounterW(mQuery, counter, 0, &mWorkingSet);

        swprintf_s(counter, L"\\Process(%s)\\Pool Nonpaged Bytes", pName);
        PdhAddCounterW(mQuery, counter, 0, &mProcessNonPaged);

        PdhAddCounterW(mQuery, L"\\Memory\\Available MBytes", 0, &mAvailableMB);
        PdhAddCounterW(mQuery, L"\\Memory\\Pool Nonpaged Bytes", 0, &mSystemNonPaged);

        // Network interfaces (wildcard: sum all adapters)
        PdhAddCounterW(mQuery, L"\\Network Interface(*)\\Bytes Received/sec", 0, &mNetRecv);
        PdhAddCounterW(mQuery, L"\\Network Interface(*)\\Bytes Sent/sec", 0, &mNetSend);

        SYSTEM_INFO si;
        GetSystemInfo(&si);
        mProcessorCount = si.dwNumberOfProcessors;

        PdhCollectQueryData(mQuery);

        // GetProcessTimes 초기값
        FILETIME ftCreation, ftExit;
        GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &mLastKernel, &mLastUser);
        GetSystemTimeAsFileTime(&mLastWall);
    }

    ~ProcessMonitor()
    {
        if (mQuery) PdhCloseQuery(mQuery);
    }

    void Update()
    {
        PdhCollectQueryData(mQuery);

        PDH_FMT_COUNTERVALUE val;

        if (PdhGetFormattedCounterValue(mPrivateBytes, PDH_FMT_LARGE, NULL, &val) == ERROR_SUCCESS)
            mPrivateRaw = val.largeValue;

        if (PdhGetFormattedCounterValue(mWorkingSet, PDH_FMT_LARGE, NULL, &val) == ERROR_SUCCESS)
            mWorkingSetRaw = val.largeValue;

        if (PdhGetFormattedCounterValue(mProcessNonPaged, PDH_FMT_LARGE, NULL, &val) == ERROR_SUCCESS)
            mProcessNonPagedKB = val.largeValue / 1024;

        if (PdhGetFormattedCounterValue(mAvailableMB, PDH_FMT_LARGE, NULL, &val) == ERROR_SUCCESS)
            mSystemAvailableRaw = val.largeValue;

        if (PdhGetFormattedCounterValue(mSystemNonPaged, PDH_FMT_LARGE, NULL, &val) == ERROR_SUCCESS)
            mSystemNonPagedKB = val.largeValue / 1024;

        // 프로세스 CPU 사용률 (GetProcessTimes)
        {
            FILETIME ftCreation, ftExit, ftKernel, ftUser, ftNow;
            GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &ftKernel, &ftUser);
            GetSystemTimeAsFileTime(&ftNow);

            ULARGE_INTEGER nowKernel, nowUser, nowWall;
            ULARGE_INTEGER lastKernel, lastUser, lastWall;

            nowKernel.LowPart = ftKernel.dwLowDateTime; nowKernel.HighPart = ftKernel.dwHighDateTime;
            nowUser.LowPart   = ftUser.dwLowDateTime;   nowUser.HighPart   = ftUser.dwHighDateTime;
            nowWall.LowPart   = ftNow.dwLowDateTime;    nowWall.HighPart   = ftNow.dwHighDateTime;

            lastKernel.LowPart = mLastKernel.dwLowDateTime; lastKernel.HighPart = mLastKernel.dwHighDateTime;
            lastUser.LowPart   = mLastUser.dwLowDateTime;   lastUser.HighPart   = mLastUser.dwHighDateTime;
            lastWall.LowPart   = mLastWall.dwLowDateTime;   lastWall.HighPart   = mLastWall.dwHighDateTime;

            ULONGLONG deltaKernel = nowKernel.QuadPart - lastKernel.QuadPart;
            ULONGLONG deltaUser   = nowUser.QuadPart   - lastUser.QuadPart;
            ULONGLONG deltaWall   = nowWall.QuadPart   - lastWall.QuadPart;

            if (deltaWall > 0)
                mCpuProcessPct = (int)((deltaKernel + deltaUser) * 100 / (deltaWall * mProcessorCount));

            mLastKernel = ftKernel;
            mLastUser   = ftUser;
            mLastWall   = ftNow;
        }

        mNetRecvKB = SumNetworkCounter(mNetRecv);
        mNetSendKB = SumNetworkCounter(mNetSend);
    }

    double  GetPrivateMB()          const { return mPrivateRaw / (1024.0 * 1024.0); }
    double  GetWorkingSetMB()       const { return mWorkingSetRaw / (1024.0 * 1024.0); }
    double  GetSystemAvailableMB()  const { return static_cast<double>(mSystemAvailableRaw); }

    int64_t GetProcessNonPagedKB()  const { return mProcessNonPagedKB; }
    int64_t GetSystemNonPagedKB()   const { return mSystemNonPagedKB; }

    int     GetCpuProccessPct()        const { return mCpuProcessPct; }
    int64_t GetNetRecvKB()          const { return mNetRecvKB; }
    int64_t GetNetSendKB()          const { return mNetSendKB; }

private:
    // Sum all network interface values and return KBytes/s
    int64_t SumNetworkCounter(PDH_HCOUNTER counter)
    {
        DWORD bufSize = 0, itemCount = 0;
        PdhGetFormattedCounterArray(counter, PDH_FMT_LARGE, &bufSize, &itemCount, nullptr);
        if (bufSize == 0) return 0;

        std::vector<BYTE> buf(bufSize);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(buf.data());
        if (PdhGetFormattedCounterArray(counter, PDH_FMT_LARGE, &bufSize, &itemCount, items) != ERROR_SUCCESS)
            return 0;

        int64_t total = 0;
        for (DWORD i = 0; i < itemCount; i++)
            total += items[i].FmtValue.largeValue;
        return total / 1024;
    }

    PDH_HQUERY   mQuery = NULL;

    PDH_HCOUNTER mPrivateBytes    = NULL;
    PDH_HCOUNTER mWorkingSet      = NULL;
    PDH_HCOUNTER mProcessNonPaged = NULL;
    PDH_HCOUNTER mAvailableMB     = NULL;
    PDH_HCOUNTER mSystemNonPaged  = NULL;
    PDH_HCOUNTER mNetRecv         = NULL;
    PDH_HCOUNTER mNetSend         = NULL;

    int64_t mPrivateRaw        = 0;
    int64_t mWorkingSetRaw     = 0;
    int64_t mSystemAvailableRaw = 0;
    int64_t mProcessNonPagedKB = 0;
    int64_t mSystemNonPagedKB  = 0;
    int     mCpuProcessPct     = 0;
    int     mProcessorCount    = 1;
    FILETIME mLastKernel       = {};
    FILETIME mLastUser         = {};
    FILETIME mLastWall         = {};
    int64_t mNetRecvKB         = 0;
    int64_t mNetSendKB         = 0;
};
