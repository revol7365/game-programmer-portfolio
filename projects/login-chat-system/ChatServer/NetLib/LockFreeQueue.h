#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

// Michael-Scott MPMC queue with two hazard pointers per participating thread.
// Nodes are retired after head CAS and reclaimed only when no hazard record
// references them, preventing the use-after-free/ABA failure of the old pool.
template <typename T>
class LockFreeQueue final {
private:
    struct Node {
        std::atomic<Node*> next{nullptr};
        std::optional<T> value;

        Node() = default;
        explicit Node(T item) : value(std::move(item)) {}
    };

    struct HazardRecord {
        std::atomic<Node*> first{nullptr};
        std::atomic<Node*> second{nullptr};
        HazardRecord* next = nullptr;
    };

    struct RetiredNode {
        Node* node = nullptr;
        RetiredNode* next = nullptr;
    };

public:
    std::atomic<std::size_t> size{0};

    LockFreeQueue() : mDomainId(sNextDomainId.fetch_add(1, std::memory_order_relaxed)) {
        static_assert(std::atomic<Node*>::is_always_lock_free,
            "This project requires lock-free pointer atomics");
        Node* sentinel = new Node();
        mHead.store(sentinel, std::memory_order_relaxed);
        mTail.store(sentinel, std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        ScanRetired();
        RetiredNode* retired = mRetired.exchange(nullptr);
        while (retired != nullptr) {
            RetiredNode* next = retired->next;
            delete retired->node;
            delete retired;
            retired = next;
        }

        Node* node = mHead.load(std::memory_order_relaxed);
        while (node != nullptr) {
            Node* next = node->next.load(std::memory_order_relaxed);
            delete node;
            node = next;
        }

        HazardRecord* record = mHazards.load(std::memory_order_relaxed);
        while (record != nullptr) {
            HazardRecord* next = record->next;
            delete record;
            record = next;
        }
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    void Enqueue(T item) {
        Node* newNode = new Node(std::move(item));
        HazardRecord* hazard = GetHazardRecord();

        for (;;) {
            Node* tail = Protect(mTail, hazard->first);
            Node* next = tail->next.load(std::memory_order_acquire);
            if (tail != mTail.load(std::memory_order_acquire)) continue;

            if (next == nullptr) {
                if (tail->next.compare_exchange_weak(next, newNode,
                    std::memory_order_release, std::memory_order_relaxed)) {
                    mTail.compare_exchange_strong(tail, newNode,
                        std::memory_order_release, std::memory_order_relaxed);
                    size.fetch_add(1, std::memory_order_relaxed);
                    ClearHazards(hazard);
                    return;
                }
            } else {
                mTail.compare_exchange_weak(tail, next,
                    std::memory_order_release, std::memory_order_relaxed);
            }
        }
    }

    bool TryDequeue(T& out) {
        HazardRecord* hazard = GetHazardRecord();

        for (;;) {
            Node* head = Protect(mHead, hazard->first);
            Node* next = head->next.load(std::memory_order_acquire);
            hazard->second.store(next, std::memory_order_seq_cst);

            if (head != mHead.load(std::memory_order_acquire)) continue;
            if (next == nullptr) {
                ClearHazards(hazard);
                return false;
            }

            Node* tail = mTail.load(std::memory_order_acquire);
            if (head == tail) {
                mTail.compare_exchange_weak(tail, next,
                    std::memory_order_release, std::memory_order_relaxed);
                continue;
            }

            T candidate = *next->value;
            if (mHead.compare_exchange_weak(head, next,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
                out = std::move(candidate);
                size.fetch_sub(1, std::memory_order_relaxed);
                ClearHazards(hazard);
                Retire(head);
                return true;
            }
        }
    }

    int Dequeue(T& out) { return TryDequeue(out) ? 0 : -1; }

    std::size_t Size() const noexcept {
        return size.load(std::memory_order_relaxed);
    }

private:
    static Node* Protect(std::atomic<Node*>& source, std::atomic<Node*>& hazard) {
        Node* value = nullptr;
        do {
            value = source.load(std::memory_order_acquire);
            hazard.store(value, std::memory_order_seq_cst);
        } while (value != source.load(std::memory_order_acquire));
        return value;
    }

    static void ClearHazards(HazardRecord* record) noexcept {
        record->second.store(nullptr, std::memory_order_release);
        record->first.store(nullptr, std::memory_order_release);
    }

    HazardRecord* GetHazardRecord() {
        for (const auto& entry : sThreadHazards) {
            if (entry.first == mDomainId) return entry.second;
        }

        HazardRecord* record = new HazardRecord();
        HazardRecord* oldHead = mHazards.load(std::memory_order_relaxed);
        do {
            record->next = oldHead;
        } while (!mHazards.compare_exchange_weak(oldHead, record,
            std::memory_order_release, std::memory_order_relaxed));

        sThreadHazards.emplace_back(mDomainId, record);
        return record;
    }

    void Retire(Node* node) {
        RetiredNode* retired = new RetiredNode{node, nullptr};
        RetiredNode* oldHead = mRetired.load(std::memory_order_relaxed);
        do {
            retired->next = oldHead;
        } while (!mRetired.compare_exchange_weak(oldHead, retired,
            std::memory_order_release, std::memory_order_relaxed));

        if ((mRetireCount.fetch_add(1, std::memory_order_relaxed) & 63u) == 63u) {
            ScanRetired();
        }
    }

    void ScanRetired() {
        RetiredNode* list = mRetired.exchange(nullptr, std::memory_order_acq_rel);
        if (list == nullptr) return;

        std::unordered_set<Node*> protectedNodes;
        for (HazardRecord* record = mHazards.load(std::memory_order_acquire);
             record != nullptr; record = record->next) {
            if (Node* node = record->first.load(std::memory_order_seq_cst)) protectedNodes.insert(node);
            if (Node* node = record->second.load(std::memory_order_seq_cst)) protectedNodes.insert(node);
        }

        RetiredNode* keep = nullptr;
        while (list != nullptr) {
            RetiredNode* next = list->next;
            if (protectedNodes.find(list->node) != protectedNodes.end()) {
                list->next = keep;
                keep = list;
            } else {
                delete list->node;
                delete list;
            }
            list = next;
        }

        while (keep != nullptr) {
            RetiredNode* item = keep;
            keep = keep->next;
            RetiredNode* oldHead = mRetired.load(std::memory_order_relaxed);
            do {
                item->next = oldHead;
            } while (!mRetired.compare_exchange_weak(oldHead, item,
                std::memory_order_release, std::memory_order_relaxed));
        }
    }

    std::atomic<Node*> mHead{nullptr};
    std::atomic<Node*> mTail{nullptr};
    std::atomic<HazardRecord*> mHazards{nullptr};
    std::atomic<RetiredNode*> mRetired{nullptr};
    std::atomic<std::size_t> mRetireCount{0};
    const std::uint64_t mDomainId;

    inline static std::atomic<std::uint64_t> sNextDomainId{1};
    inline static thread_local std::vector<std::pair<std::uint64_t, HazardRecord*>> sThreadHazards;
};
