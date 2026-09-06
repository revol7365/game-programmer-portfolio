#pragma once
#include <winsock2.h>
#include "Packet.h"
#include "Log.h"

template <class T>
class RingBuffer {
private:
	int _front, _rear;
	int _bufferSize; // 버퍼 크기
	T* _data; // 버퍼 시작 포인터

public:
	RingBuffer() {
		_front = 0;
		_rear = 0;
		_bufferSize = 0;
		_data = nullptr;
	}

	RingBuffer(int iBufferSize) {
		_bufferSize = iBufferSize + 1;
		_data = new T[_bufferSize];
		_front = 0;
		_rear = 0;
	}

	~RingBuffer() {
		if (_data != nullptr) {
			delete[] _data;
			_data = nullptr;
		}
	}

	void Resize(int size) {
		if (_data) {
			delete[] _data;
		}
		_bufferSize = size + 1;
		_data = new T[_bufferSize];
		if (!_data) {
			_bufferSize = 0;
			return;
		}
		_front = 0;
		_rear = 0;
	}

	int	GetBufferSize() {
		return _bufferSize - 1;
	}

	// 현재 사용중인 용량 얻기.
	// Parameters: 없음.
	// Return: (int)사용중인 용량.
	int	GetUseSize() {
		return (_rear - _front + _bufferSize) % _bufferSize;
	}

	// 현재 버퍼에 남은 용량 얻기. 
	// Parameters: 없음.
	// Return: (int)남은용량.
	int	GetFreeSize() {
		return GetBufferSize() - GetUseSize();
	}

	// WritePos 에 데이타 넣음.
	// Parameters: (char *)데이타 포인터. (int)크기. 
	// Return: (int)넣은 크기.
	int	Enqueue(T* chpData, int iSize) {
		if (!chpData || iSize <= 0) return 0;

		int freeSpace = GetFreeSize();
		int writeSize = iSize < freeSpace ? iSize : freeSpace;

		// first copy: rear → 끝까지 연속 공간
		int firstChunk = writeSize < _bufferSize - _rear ? writeSize : _bufferSize - _rear;
		memcpy(&_data[_rear], chpData, firstChunk);
		_rear = (_rear + firstChunk) % _bufferSize;

		// second copy: 버퍼 앞부분으로 wrap-around
		int remaining = writeSize - firstChunk;
		if (remaining > 0) {
			memcpy(&_data[_rear], chpData + firstChunk, remaining);
			_rear = (_rear + remaining) % _bufferSize;
		}

		return writeSize;
	}

	int EnqueueItem(const T& item) {
		if (GetFreeSize() < 1)
		{
			LOG(L"[SendPacket] sendRingBuffer FULL!");
			return 0;
		}
		_data[_rear] = item;
		_rear = (_rear + 1) % _bufferSize;
		return 1;
	}

	// ReadPos 에서 데이타 가져옴. ReadPos 이동.
	// Parameters: (char *)데이타 포인터. (int)크기.
	// Return: (int)가져온 크기.
	int	Dequeue(T* chpDest, int iSize) {
		if (!chpDest || iSize <= 0) return 0;

		int useSize = GetUseSize();
		int readSize = (iSize < useSize) ? iSize : useSize;

		// first chunk: front부터 끝까지 연속 읽기
		int firstChunk = (_bufferSize - _front < readSize) ? (_bufferSize - _front) : readSize;
		memcpy(chpDest, &_data[_front], firstChunk);
		_front = (_front + firstChunk) % _bufferSize;

		// second chunk: 버퍼 앞부분 wrap-around
		int remaining = readSize - firstChunk;
		if (remaining > 0) {
			memcpy(chpDest + firstChunk, &_data[_front], remaining);
			_front = (_front + remaining) % _bufferSize;
		}

		return readSize;
	}

	bool DequeueItem(T* outItem) {
		if (GetUseSize() <= 0) return false;

		*outItem = _data[_front];
		_front = (_front + 1) % _bufferSize;
		return true;
	}

	// ReadPos 에서 데이타 읽어옴. ReadPos 고정.
	// Parameters: (char *)데이타 포인터. (int)크기.
	// Return: (int)가져온 크기.
	int	Peek(T* chpDest, int iSize) {
		if (!chpDest || iSize <= 0) return 0;

		int useSize = GetUseSize();
		int peekSize = (iSize < useSize) ? iSize : useSize;

		int firstChunk = (_bufferSize - _front < peekSize) ? (_bufferSize - _front) : peekSize;
		memcpy(chpDest, &_data[_front], firstChunk);

		int remaining = peekSize - firstChunk;
		if (remaining > 0) {
			memcpy(chpDest + firstChunk, &_data[0], remaining);
		}

		return peekSize;
	}

	// 버퍼의 모든 데이타 삭제.
	void ClearBuffer(void) {
		_front = 0;
		_rear = 0;
	}

	// 버퍼 포인터로 외부에서 한방에 읽고, 쓸 수 있는 길이.
	// (끊기지 않은 길이)
	// 원형 큐의 구조상 버퍼의 끝단에 있는 데이터는 끝 -> 처음으로 돌아가서
	// 2번에 데이터를 얻거나 넣을 수 있음. 이 부분에서 끊어지지 않은 길이를 의미
	// Return: (int)사용가능 용량.
	int	DirectEnqueueSize() {
		if (GetFreeSize() == 0) {
			return 0; // Not enough space
		}
		if (_rear >= _front) {
			// rear가 앞쪽, front가 뒤쪽: 끝까지 쓰되 front 앞까지만 가능
			int spaceToEnd = _bufferSize - _rear;
			if (_front == 0)
				return spaceToEnd - 1; // front가 0이면 마지막 칸 비워야 함
			return (_front > _rear) ? (_front - _rear - 1) : (spaceToEnd);
		}
		else {
			// front가 뒤에 있어서 연속 공간 확보 가능
			return _front - _rear - 1;
		}
	}

	int	DirectDequeueSize() {
		if (GetUseSize() == 0) {
			return 0; // Not enough data
		}
		if (_front < _rear) {
			// front → rear까지 연속
			return _rear - _front;
		}
		else {
			// rear가 front 앞쪽 → 끝까지 읽고 돌아가야 함
			return _bufferSize - _front;
		}
	}

	// 원하는 길이만큼 읽기위치 에서 삭제 / 쓰기 위치 이동
	// Return: (int)이동크기
	int	MoveRear(int iSize) {
		if (iSize > GetFreeSize()) {
			return 0; // Not enough space
		}
		_rear = (_rear + iSize) % _bufferSize;
		return iSize;
	}

	int	MoveFront(int iSize) {
		if (iSize > GetUseSize()) {
			return 0; // Not enough data
		}
		_front = (_front + iSize) % _bufferSize;
		return iSize;
	}

	// 버퍼의 Front 포인터 얻음.
	T* GetFrontBufferPtr() {
		return &_data[_front];
	}

	// 버퍼의 RearPos 포인터 얻음.
	T* GetRearBufferPtr(void) {
		return &_data[_rear];
	}

	int GetFrontIndex() const {
		return _front;
	}

	int GetRearIndex() const {
		return _rear;
	}

	// IOCP Recv를 위한 함수
	int GetEnqueueSegments(WSABUF* wsaBufArray)
		requires std::same_as<T, char>
	{

		// 남는 공간 확인
		int freeSize = GetFreeSize();
		if (freeSize <= 0) {
			// 공간이 없으면 0개 반환
			wsaBufArray[0].len = 0;
			wsaBufArray[1].len = 0;
			return 0;
		}

		// 끊히지 않는 연속된 길이
		int segment1_len = DirectEnqueueSize();
		if (segment1_len > freeSize) {
			segment1_len = freeSize;
		}

		// 첫번째 부분 설정
		wsaBufArray[0].buf = GetRearBufferPtr();
		wsaBufArray[0].len = (ULONG)segment1_len;

		// 두번째 부분 설정
		int segment2_len = freeSize - segment1_len;

		if (segment2_len > 0) {
			wsaBufArray[1].buf = _data;
			wsaBufArray[1].len = (ULONG)segment2_len;
			return 2;
		}
		else {
			wsaBufArray[1].buf = nullptr;
			wsaBufArray[1].len = 0;
			return 1; // 하나의 버퍼 사용
		}
	}

	// IOCP send를 위한 함수
	int GetDequeueSegments(WSABUF* wsaBufArray)
		requires std::same_as<T, char>
	{
		// 현재 사용 중인 데이터의 총량 확인
		int useSize = GetUseSize();
		if (useSize <= 0) {
			// 읽을 데이터가 없으면 0개 반환
			wsaBufArray[0].len = 0;
			wsaBufArray[1].len = 0;
			return 0;
		}

		// DirectDequeueSize: 현재 front부터 버퍼 끝까지의 연속된 길이 (L1)
		int segment1_len = DirectDequeueSize();

		// segment1_len이 GetUseSize()보다 클 수 없습니다.
		// 만약 DirectDequeueSize가 사용 중인 전체 용량보다 크다면 (발생해서는 안 되지만) 
		// 사용 중인 전체 용량으로 제한합니다.
		if (segment1_len > useSize) {
			segment1_len = useSize;
		}

		// --- WSABUF[0] 설정 (첫 번째 연속 영역) ---
		// 현재 읽기 포인터 (_front)부터 버퍼 끝까지의 영역
		wsaBufArray[0].buf = GetFrontBufferPtr(); // _data + _front
		wsaBufArray[0].len = (ULONG)segment1_len;

		int segment2_len = useSize - segment1_len;
		if (segment2_len > 0) {
			wsaBufArray[1].buf = _data; // 버퍼 시작 주소
			wsaBufArray[1].len = (ULONG)segment2_len;
			return 2; // 두 개의 버퍼 사용
		}
		else {
			wsaBufArray[1].buf = nullptr;
			wsaBufArray[1].len = 0;
			return 1; // 하나의 버퍼 사용
		}
	}

	int GetDequeueSegments(WSABUF* wsaBufArray)
		requires std::same_as<T, Packet*>
	{
		int useSize = GetUseSize();
		if (useSize <= 0) return 0;

		int count = 0;
		while (count < useSize && count < 100) {
			Packet* packet = _data[_front]; // 링버퍼에서 포인터를 꺼냄

			if (packet == nullptr) break;

			wsaBufArray[count].buf = packet->GetBufferPtr();
			wsaBufArray[count].len = static_cast<ULONG>(packet->GetDataSize());

			_front = (_front + 1) % _bufferSize;
			count++;
		}

		return count;
	}

};