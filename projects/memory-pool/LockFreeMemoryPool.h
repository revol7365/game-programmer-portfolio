#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

// Windows SLIST is an ABA-resistant, lock-free LIFO on x64.  Every returned
// object stays constructed while it belongs to the pool; callers reset its
// logical state before reuse (the server Packet type follows this contract).
template <typename T>
class LockFreeMemoryPool final {
private:
    static constexpr std::uint64_t kCookie = 0xC0DEC0DEF00DBAADull;

    struct alignas(MEMORY_ALLOCATION_ALIGNMENT) Node {
        SLIST_ENTRY entry{};
        LockFreeMemoryPool* owner = nullptr;
        std::uint64_t cookie = kCookie;
        T value{};
    };

    static_assert(alignof(Node) >= MEMORY_ALLOCATION_ALIGNMENT,
        "SLIST nodes must satisfy Windows alignment requirements");

public:
    explicit LockFreeMemoryPool(std::size_t initialCount = 0) {
        InitializeSListHead(&mFreeList);
        Reserve(initialCount);
    }

    ~LockFreeMemoryPool() {
        // Destruction is only valid after worker threads have joined.
        if (mInUse.load(std::memory_order_acquire) != 0) {
            // Avoid freeing checked-out objects. This also makes a lifetime bug
            // visible to a debugger instead of turning it into use-after-free.
            return;
        }

        PSLIST_ENTRY entry = InterlockedFlushSList(&mFreeList);
        while (entry != nullptr) {
            PSLIST_ENTRY next = entry->Next;
            delete FromEntry(entry);
            entry = next;
        }
    }

    LockFreeMemoryPool(const LockFreeMemoryPool&) = delete;
    LockFreeMemoryPool& operator=(const LockFreeMemoryPool&) = delete;

    template <typename... Args>
    T* Alloc(Args&&... args) {
        PSLIST_ENTRY entry = InterlockedPopEntrySList(&mFreeList);
        Node* node = entry ? FromEntry(entry) : CreateNode();

        if constexpr (sizeof...(Args) > 0) {
            node->value = T(std::forward<Args>(args)...);
        }

        mInUse.fetch_add(1, std::memory_order_relaxed);
        return &node->value;
    }

    bool Free(T* value) noexcept {
        if (value == nullptr) {
            return false;
        }

        Node* node = FromValue(value);
        if (node->owner != this || node->cookie != kCookie) {
            return false;
        }

        InterlockedPushEntrySList(&mFreeList, &node->entry);
        mInUse.fetch_sub(1, std::memory_order_release);
        return true;
    }

    void Reserve(std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            Node* node = CreateNode();
            InterlockedPushEntrySList(&mFreeList, &node->entry);
        }
    }

    std::size_t GetCapacity() const noexcept {
        return mCapacity.load(std::memory_order_relaxed);
    }

    std::size_t GetUsedCount() const noexcept {
        return mInUse.load(std::memory_order_acquire);
    }

    std::size_t GetAvailableCount() noexcept {
        return static_cast<std::size_t>(QueryDepthSList(&mFreeList));
    }

private:
    Node* CreateNode() {
        Node* node = new Node{};
        node->owner = this;
        mCapacity.fetch_add(1, std::memory_order_relaxed);
        return node;
    }

    static Node* FromEntry(PSLIST_ENTRY entry) noexcept {
        return CONTAINING_RECORD(entry, Node, entry);
    }

    static Node* FromValue(T* value) noexcept {
        auto* bytes = reinterpret_cast<unsigned char*>(value);
        return reinterpret_cast<Node*>(bytes - offsetof(Node, value));
    }

    SLIST_HEADER mFreeList{};
    std::atomic<std::size_t> mCapacity{0};
    std::atomic<std::size_t> mInUse{0};
};
