#pragma once
#include <queue>
#include <atomic>
#include "Define.h"

/*
	MemoryPool
	- 정적 TLS
	- 전역풀
	- 전역풀 동기화 방법 - LockFree
	- Node 포인터에 포인터를 추가해서 관리를 한다
*/


template <class T>
class MemoryPoolTLS {
public:
#pragma pack(push, 1)
	struct Node {
		//uint32_t  underFlowGuard = 0xFFFFFFFF;
		T data;
		//uint32_t  overFlowGuard = 0xFFFFFFFF;
		Node* nextNode = nullptr;
		//void* securityCookie = nullptr;
	};
#pragma pack(pop)

private:
	static const uint64_t PTR_MASK = 0x0000FFFFFFFFFFFF;
	static const uint64_t ID_MASK = 0xFFFF000000000000;
	static const uint64_t ID_UNIT = 0x0001000000000000;

	// ---------------- TLS 버전  시작----------------
	static constexpr int MAX_POOL_INSTANCES = 64;
	static inline std::atomic<int> s_poolIDGenerator{ 0 };
	int _poolIdx; // 이 인스턴스가 사용할 고유 인덱스

	int _initCount;  // 초기 할당 수 (Shrink 시 유지할 최소 수)
	alignas(64) std::atomic<long> mUseCount = 0;   // 현재 할당 중인 개수
	alignas(64) std::atomic<long> mAllocCount = 0;  // 전체 노드 개수 (Free + Use)

	struct alignas(64) ThreadLocal {
		Node* top = nullptr;
		int count = 0;
	};

	struct alignas(64) ThreadBatch {
		std::atomic<uint64_t> batchTop{ 0 };
		std::atomic<int> batchCount{ 0 };
	};

	struct alignas(64) ThreadData {
		ThreadLocal local;      // TLS 대체 (이 스레드만 접근)
		ThreadBatch batch;      // 공유 (다른 스레드가 훔칠 수 있음)
	};

	static inline ThreadData s_threadData[MAX_POOL_INSTANCES][MAX_THREADS];
	static inline thread_local int t_threadIdx = -1;
	static inline std::atomic<int> s_threadCount{ 0 };
	// ---------------- TLS 버전  끝----------------


	// ---------------- 일반 버전 시작----------------
	// Global 모드일 때만 사용하는 원자적 변수 저장소
	struct GlobalStorage {
		std::atomic<uint64_t> _top{ 0 };
		std::atomic<int> _availableCount{ 0 };
	};
	GlobalStorage _storage;
	// ---------------- 일반 버전 끝----------------

public:
	MemoryPoolTLS(int initCount = INITNODESIZE)
		: _initCount(initCount)
	{
		_poolIdx = s_poolIDGenerator.fetch_add(1);
		if (_poolIdx >= MAX_POOL_INSTANCES) __debugbreak(); // 인스턴스 초과
		_storage._availableCount.store(0);
		ReserveNodes(initCount);
	};

	virtual ~MemoryPoolTLS()
	{
		int threadCount = s_threadCount.load();

		for (int i = 0; i < threadCount; i++) {
			ThreadData& data = s_threadData[_poolIdx][i];

			// Local 풀 정리
			Node* current = data.local.top;
			while (current) {
				Node* next = current->nextNode;
				delete current;
				current = next;
			}
			data.local.top = nullptr;
			data.local.count = 0;

			// Batch 풀 정리
			uint64_t topVal = data.batch.batchTop.load();
			Node* batch = reinterpret_cast<Node*>(topVal & PTR_MASK);
			while (batch) {
				Node* next = batch->nextNode;
				delete batch;
				batch = next;
			}
			data.batch.batchTop.store(0);
			data.batch.batchCount.store(0);
		}

		// Global Pool 정리
		uint64_t globalTopVal = _storage._top.load();
		Node* globalNode = reinterpret_cast<Node*>(globalTopVal & PTR_MASK);
		while (globalNode) {
			Node* next = globalNode->nextNode;
			delete globalNode;
			globalNode = next;
		}
		_storage._top.store(0);
		_storage._availableCount.store(0);
	}

	// 새로운 포인터를 넘겨줌
	T* Alloc() {

		int tidx = GetThreadIdx();
		ThreadData& myData = s_threadData[_poolIdx][tidx];
		Node* oldNode = myData.local.top;

		// TLS 풀
		if (oldNode) {
			myData.local.top = oldNode->nextNode;
			myData.local.count--;
			mUseCount.fetch_add(1);
			return &oldNode->data;
		}

		// 내 Batch 풀
		int count = 0;
		Node* batch = PopBatchFromMyBatch(count);
		if (batch) {
			oldNode = batch;
			myData.local.top = batch->nextNode;
			myData.local.count = count - 1;
			mUseCount.fetch_add(1);
			return &oldNode->data;
		}

		// 다른 스레드에서 훔치기
		Node* stolen = StealBatch();
		if (stolen) {
			oldNode = stolen;
			myData.local.top = stolen->nextNode;
			int stolenCount = 0;
			Node* p = myData.local.top;
			while (p) { stolenCount++; p = p->nextNode; }
			myData.local.count = stolenCount;
			mUseCount.fetch_add(1);
			return &oldNode->data;
		}

		// 3-1. 재스캔 (첫 스캔 사이 batch에 도착한 노드 포착)
		stolen = StealBatch();
		if (stolen) {
			oldNode = stolen;
			myData.local.top = stolen->nextNode;
			int stolenCount = 0;
			Node* p = myData.local.top;
			while (p) { stolenCount++; p = p->nextNode; }
			myData.local.count = stolenCount;
			mUseCount.fetch_add(1);
			return &oldNode->data;
		}

		// Global Pool에서 가져오기
		{
			int globalCount = 0;
			Node* globalBatch = PopFromGlobal(globalCount);
			if (globalBatch) {
				oldNode = globalBatch;
				myData.local.top = globalBatch->nextNode;
				myData.local.count = globalCount - 1;
				mUseCount.fetch_add(1);
				return &oldNode->data;
			}
		}

		// 새로 생성
		ReserveNodes(MOVENODESIZE);
		oldNode = myData.local.top;
		if (!oldNode) return nullptr;

		myData.local.top = oldNode->nextNode;
		myData.local.count--;
		mUseCount.fetch_add(1);
		return &oldNode->data;
	}

	// 반납
	bool Free(T* returnData) {

		if (returnData == nullptr) return false;
		size_t offset = offsetof(Node, data);
		Node* freeNode = reinterpret_cast<Node*>(reinterpret_cast<char*>(returnData) - offset);

		// 필터링
		if (freeNode == nullptr) return false;
		//if (this != freeNode->securityCookie) return false;
		//if (freeNode->underFlowGuard != 0xFFFFFFFF) return false;
		//if (freeNode->overFlowGuard != 0xFFFFFFFF) return false;

		// 풀에 반납
		int tidx = GetThreadIdx();
		ThreadData& myData = s_threadData[_poolIdx][tidx];

		// MOVENODESIZE를 넘으면 Global Pool에 반환
		if (myData.local.count > MOVENODESIZE)
		{
			PushToGlobal(freeNode);
		}
		else {
			freeNode->nextNode = myData.local.top;
			myData.local.top = freeNode;
			myData.local.count++;
		}

		mUseCount.fetch_sub(1);
		return true;
	}

	// 뭉치 추가
	// 아예 노드가 없을 때만 호출
	// return 되는 node는 연결되어있지 않은 Node
	void ReserveNodes(int count)
	{
		if (count <= 0) return;

		Node* batchHead = nullptr;
		Node* batchTail = nullptr;

		//  노드 생성
		for (int i = 0; i < count; i++)
		{
			Node* newNode = new Node();
			if (batchTail == nullptr) batchTail = newNode; // 가장 먼저 만든 게 꼬리가 됨

			newNode->nextNode = batchHead; // 새 노드가 기존 머리를 가리킴
			//newNode->securityCookie = this;
			batchHead = newNode;           // 새 노드가 새로운 머리가 됨
		}
		mAllocCount.fetch_add(count);

		int tidx = GetThreadIdx();
		ThreadData& myData = s_threadData[_poolIdx][tidx];
		batchTail->nextNode = myData.local.top;
		myData.local.top = batchHead;
		myData.local.count += count;
	}

	static void ResetThreadCount()
	{
		s_threadCount.store(0);
	}

	// ==================== 스레드 인덱스 획득 ====================
	static int GetThreadIdx() {
		if (t_threadIdx == -1) {
			t_threadIdx = s_threadCount.fetch_add(1);
			if (t_threadIdx >= MAX_THREADS) __debugbreak();
		}
		return t_threadIdx;
	}

	// ==================== 내 Batch에 Push ====================
	void PushToMyBatch(Node* node) {
		int tidx = GetThreadIdx();
		ThreadBatch& myBatch = s_threadData[_poolIdx][tidx].batch;

		uint64_t oldTop = myBatch.batchTop.load(std::memory_order_relaxed);
		uint64_t newTop;

		do {
			node->nextNode = reinterpret_cast<Node*>(oldTop & PTR_MASK);
			newTop = ((oldTop & ID_MASK) + ID_UNIT) | reinterpret_cast<uintptr_t>(node);
		} while (!myBatch.batchTop.compare_exchange_weak(oldTop, newTop,
			std::memory_order_release, std::memory_order_relaxed));

		myBatch.batchCount.fetch_add(1, std::memory_order_relaxed);
	}

	// ==================== 다른 스레드에서 훔치기 (Lock-free) ====================
	// 하나의 타겟 실패 시 다음 타겟으로 순회
	Node* StealBatch() {
		int myIdx = GetThreadIdx();
		int threadCount = s_threadCount.load(std::memory_order_relaxed);

		for (int i = 0; i < threadCount; i++) {
			if (i == myIdx) continue;

			ThreadBatch& target = s_threadData[_poolIdx][i].batch;
			int count = target.batchCount.load(std::memory_order_relaxed);
			if (count <= 0) continue;

			int stealCount = (count >= MOVENODESIZE) ? MOVENODESIZE : count;
			uint64_t oldTop = target.batchTop.load(std::memory_order_acquire);

			while (true) {
				Node* topNode = reinterpret_cast<Node*>(oldTop & PTR_MASK);
				if (!topNode) break; // 이 타겟은 비었음 → 다음 타겟 시도

				// stealCount만큼 따라가기
				Node* tail = topNode;
				int actualCount = 1;
				for (int j = 1; j < stealCount; j++) {
					if (!tail->nextNode) break;
					tail = tail->nextNode;
					actualCount++;
				}

				Node* remaining = tail->nextNode;
				uint64_t newTop;
				if (remaining) {
					newTop = ((oldTop & ID_MASK) + ID_UNIT) | reinterpret_cast<uintptr_t>(remaining);
				}
				else {
					newTop = ((oldTop & ID_MASK) + ID_UNIT); // 전부 가져감
				}

				if (target.batchTop.compare_exchange_weak(oldTop, newTop)) {
					target.batchCount.fetch_sub(actualCount);
					tail->nextNode = nullptr;
					return topNode;
				}
				// CAS 실패 시 oldTop이 갱신됨 → 같은 타겟 재시도
			}
			// topNode == nullptr로 break → 다음 타겟 시도
		}
		return nullptr;
	}

	// ==================== Global Pool에 Push ====================
	void PushToGlobal(Node* node) {
		uint64_t oldTop = _storage._top.load(std::memory_order_relaxed);
		uint64_t newTop;

		do {
			node->nextNode = reinterpret_cast<Node*>(oldTop & PTR_MASK);
			newTop = ((oldTop & ID_MASK) + ID_UNIT) | reinterpret_cast<uintptr_t>(node);
		} while (!_storage._top.compare_exchange_weak(oldTop, newTop,
			std::memory_order_release, std::memory_order_relaxed));

		_storage._availableCount.fetch_add(1, std::memory_order_relaxed);
	}

	// ==================== Global Pool에서 Pop (배치) ====================
	Node* PopFromGlobal(int& outCount) {
		int available = _storage._availableCount.load(std::memory_order_relaxed);
		if (available <= 0) {
			outCount = 0;
			return nullptr;
		}

		int popCount = (available >= MOVENODESIZE) ? MOVENODESIZE : available;
		uint64_t oldTop = _storage._top.load(std::memory_order_acquire);

		while (true) {
			Node* topNode = reinterpret_cast<Node*>(oldTop & PTR_MASK);
			if (!topNode) {
				outCount = 0;
				return nullptr;
			}

			// popCount만큼 따라가기
			Node* tail = topNode;
			int actualCount = 1;
			for (int j = 1; j < popCount; j++) {
				if (!tail->nextNode) break;
				tail = tail->nextNode;
				actualCount++;
			}

			Node* remaining = tail->nextNode;
			uint64_t newTop;
			if (remaining) {
				newTop = ((oldTop & ID_MASK) + ID_UNIT) | reinterpret_cast<uintptr_t>(remaining);
			}
			else {
				newTop = ((oldTop & ID_MASK) + ID_UNIT);
			}

			if (_storage._top.compare_exchange_weak(oldTop, newTop,
				std::memory_order_acquire, std::memory_order_relaxed)) {
				_storage._availableCount.fetch_sub(actualCount, std::memory_order_relaxed);
				tail->nextNode = nullptr;
				outCount = actualCount;
				return topNode;
			}
		}
	}

	// ==================== 내 Batch에서 가져오기 ====================
	Node* PopBatchFromMyBatch(int& outCount) {
		int tidx = GetThreadIdx();
		ThreadBatch& myBatch = s_threadData[_poolIdx][tidx].batch;

		uint64_t oldTop = myBatch.batchTop.load(std::memory_order_acquire);

		while (true) {
			Node* topNode = reinterpret_cast<Node*>(oldTop & PTR_MASK);
			if (!topNode) {
				outCount = 0;
				return nullptr;
			}

			uint64_t newTop = ((oldTop & ID_MASK) + ID_UNIT);

			if (myBatch.batchTop.compare_exchange_weak(oldTop, newTop,
				std::memory_order_acquire, std::memory_order_relaxed)) {
				outCount = myBatch.batchCount.exchange(0, std::memory_order_relaxed);
				return topNode;
			}
		}
	}

	int GetAvailableCount(void) {
		int tidx = GetThreadIdx();
		return s_threadData[_poolIdx][tidx].local.count;
	}

	void DebugPrint(const char* name) {
		long alloc = mAllocCount.load();
		long free = mAllocCount.load() - mUseCount.load();
		printf("[%s] Alloc: %ld, Free: %ld, 미반납: %ld\n",
			name, alloc, free, alloc - free);
	}

	// ==================== Shrink ====================
	// Global Pool에서 초과 유휴 노드를 delete하여 OS에 메모리 반환
	int Shrink() {
		int available = _storage._availableCount.load();
		if (available <= _initCount) return 0;

		int targetDelete = available - _initCount;
		int deleted = 0;

		while (deleted < targetDelete) {
			int batchCount = 0;
			Node* batch = PopFromGlobal(batchCount);
			if (!batch) break;

			Node* current = batch;
			while (current) {
				Node* next = current->nextNode;
				delete current;
				deleted++;
				current = next;
			}
		}

		mAllocCount.fetch_sub(deleted);
		return deleted;
	}

	// ==================== Monitor ====================
	long GetAllocCount() const { return mAllocCount.load(); }
	long GetUseCount() const { return mUseCount.load(); }
	long GetFreeCount() const { return mAllocCount.load() - mUseCount.load(); }

private:
	// 유저 메모리 영역 체크 (상위 16비트가 0인지)
	bool IsValidUserPtr(Node* ptr) {
		if (ptr == nullptr) return true;
		return ((uintptr_t)ptr & ID_MASK) == 0;
	}

	// 오프셋 -> 실제 포인터 변환
	Node* GetPtr(uint64_t headValue) {
		return (Node*)(headValue & PTR_MASK);
	}
};
