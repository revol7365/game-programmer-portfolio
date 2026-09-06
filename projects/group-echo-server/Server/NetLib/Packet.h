#pragma once
#include <concepts>
#include <type_traits>
#include <cstring>
#include <cassert>
#include <atomic>
#include <cstdint>
#include "MemoryPoolTLS.h"
#include "Log.h"

#pragma pack(push, 1)
struct PACKET_HEADER {
    unsigned char  Code;      // 패킷 시작을 알리는 고정 코드 (예: 0x77)
    unsigned short Len;       // Payload(데이터)의 크기
    unsigned char  RandKey;   // 암호화에 사용된 랜덤 키
    unsigned char  CheckSum;  // 데이터 검증용 체크섬
};
#pragma pack(pop)


// 1. 우리가 허용할 타입의 조건을 정의합니다.
template <typename T>
concept PacketData = (std::is_arithmetic_v<T> || std::is_trivially_copyable_v<T>) && !std::is_pointer_v<T>;

class Packet {
public:
    //enum { BUFFER_DEFAULT = 1024 * 5 };
    enum { BUFFER_DEFAULT = 512 };

    inline static MemoryPoolTLS<Packet> packetMemoryPool{ 1024 * 10 };

private:
    uint64_t mSessionID = 0;
    alignas(8) char mStorage[BUFFER_DEFAULT];
    int mDataSize = 0;
    int mReadPos = 0;
    std::atomic<int> mRefCnt{ 0 };
    std::atomic<bool> mIsEncoding{ 0 };
    CRITICAL_SECTION encodingCS;

public:

    Packet() : mStorage{} {
        InitializeCriticalSection(&encodingCS);
    }

    ~Packet()
    {
        DeleteCriticalSection(&encodingCS);
    }

    Packet(const Packet&) = delete;
    Packet& operator=(const Packet&) = delete;

    inline void Clear() {
        mDataSize = 0;
        mReadPos = 0;
        mSessionID = 0;
        // mRefCnt.store(1);
        mIsEncoding.store(false);
        // memset(mStorage, 0, sizeof(mStorage));  -- 성능을 위해
    }

    void Init() {
        Clear();
        mRefCnt.store(1);  // 여기서만 설정
    }

    uint64_t GetSessionID() const { return mSessionID; }
    int GetDataSize() const { return mDataSize; }
    int GetRefCnt() const { return mRefCnt.load(); }
    bool GetIsEncoding() const { return mIsEncoding.load(); }

    void SetSessionID(uint64_t sessionID) { mSessionID = sessionID; }

    void SetDataSize(int dataSize) {
        if (dataSize < 0 || dataSize > BUFFER_DEFAULT) {
            assert(false && "Invalid DataSize");
            return;
        }
        mDataSize = dataSize;
    }

    void SetReadPos(int readPos) {
        if (readPos < 0 || readPos > mDataSize) {
            assert(false && "Invalid ReadPos");
            return;
        }
        mReadPos = readPos;
    }
    void SetIsEncoding(bool isEncoding) { mIsEncoding = isEncoding; }

    void AddRef() { mRefCnt.fetch_add(1); }

    int SubRef() {
        int prevCnt = mRefCnt.fetch_sub(1);
        int cnt = prevCnt - 1;
        if (cnt == 0) {
            packetMemoryPool.Free(this);
        }
        return cnt;
    }

    char* GetBufferPtr() { return mStorage; }
    const char* GetBufferPtr() const { return mStorage; }

    void Lock()
    {
        EnterCriticalSection(&encodingCS);
    }

    void Unlock()
    {
        LeaveCriticalSection(&encodingCS);
    }

    // 2. operator<< 에 concept 적용
    template <PacketData T>
    inline Packet& operator<<(T value) {
        // 버퍼 오버플로우 체크 (필수!)
        if (static_cast<size_t>(mDataSize) + sizeof(T) > BUFFER_DEFAULT) {
            assert(false && "Packet Buffer Overflow");
            return *this;
        }

        std::memcpy(mStorage + mDataSize, &value, sizeof(T));
        mDataSize += sizeof(T);
        return *this;
    }

    // 3. operator>> 에 concept 적용
    template <PacketData T>
    inline Packet& operator>>(T& value) {
        if (mReadPos + sizeof(T) > static_cast<size_t>(mDataSize)) {
            LOG(L"[UNDERFLOW] readPos=%d, sizeof=%zu, dataSize=%d, sessionID=%llu\n",
                mReadPos, sizeof(T), mDataSize, mSessionID);
            assert(false && "Packet Buffer Underflow");
            return *this;
        }

        std::memcpy(&value, mStorage + mReadPos, sizeof(T));
        mReadPos += sizeof(T);
        return *this;
    }

    void MoveReadPos(int size) {
        if (mReadPos + size < 0 || mReadPos + size > mDataSize) {
            assert(false && "Invalid MoveReadPos Range");
            return;
        }
        mReadPos += size;
    }

    // 5. 데이터 직접 꺼내기 (문자열 등 추출용)
    // 현재 mReadPos 위치에서 지정한 size만큼 데이터를 dest로 복사합니다.
    inline int GetData(void* dest, int size) {
        if (mReadPos + size > mDataSize) {
            // 읽을 수 있는 만큼만 처리 (Underflow 방지)
            int copySize = mDataSize - mReadPos;
            if (copySize <= 0) return 0;

            std::memcpy(dest, mStorage + mReadPos, copySize);
            mReadPos += copySize;
            return copySize;
        }

        std::memcpy(dest, mStorage + mReadPos, size);
        mReadPos += size;
        return size;
    }

    inline int PutData(const void* src, int size) {
        // 남은 공간 체크
        if (mDataSize + size > BUFFER_DEFAULT) {
            // 넣을 수 있는 만큼만 넣거나, 실패 처리
            int copySize = BUFFER_DEFAULT - mDataSize;
            if (copySize <= 0) return 0;

            std::memcpy(mStorage + mDataSize, src, copySize);
            mDataSize += copySize;
            return copySize;
        }

        std::memcpy(mStorage + mDataSize, src, size);
        mDataSize += size;
        return size;
    }
};