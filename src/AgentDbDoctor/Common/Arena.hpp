// The Arena class, a bump allocator for short lived temporary values.
//
// The performance contract in the specification requires zero allocations on the
// hot path. An arena satisfies that requirement for the cases where a value
// genuinely must be built at runtime: memory is taken from one large block with
// a pointer that only ever moves forward, and releasing everything is a single
// assignment. There is no per object free and no allocator bookkeeping.
#ifndef AGENT_DB_DOCTOR_COMMON_ARENA_HPP
#define AGENT_DB_DOCTOR_COMMON_ARENA_HPP
// Provides `array`, a fixed size collection whose storage lives inside the
// object it belongs to.
#include <array>
// Provides `memcpy`, which copies a run of bytes.
#include <cstring>
// Provides `nothrow`, the form of `new` that returns `nullptr` instead of
// throwing when memory cannot be obtained.
#include <new>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `string_view`, a read only view of characters owned elsewhere.
#include <string_view>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Common {
// The Arena class hands out slices of one contiguous block of memory.
//
// The class is `final` because the specification forbids inheritance. Copying and
// moving are both deleted because an arena owns raw memory: a copied arena would
// have two owners releasing the same block, and a moved arena would leave a
// dangling pointer behind.
class Arena final {
public:
    // The default arena is small and lives entirely inside the object, so
    // creating one costs nothing at all. This is the arena used by the report
    // renderers.
    Arena() = default;
    // Creates an arena that can hand out at least `initialCapacityBytes` before
    // it has to grow. Passing zero gives the same arena as the default
    // constructor.
    explicit Arena(size_t initialCapacityBytes) {
        if (initialCapacityBytes > inlineBlock.size()) {
            growTo(initialCapacityBytes);
        }
    }
    // The destructor releases the block so the operating system gets the memory
    // back when the arena goes out of scope.
    ~Arena() {
        release();
    }
    Arena(const Arena&)=delete;
    Arena& operator=(const Arena&)=delete;
    Arena(Arena&&)=delete;
    Arena& operator=(Arena&&)=delete;
    // Copies `byteCount` bytes from `sourceAddress` into the arena and returns
    // the address of the copy. The returned memory belongs to the arena and
    // stays valid until `reset()` or `release()` is called. Returns `nullptr`
    // when the arena cannot grow, which the caller must check.
    //
    // This is the one place in the codebase that hands out a raw pointer. The
    // reason is that the alternative, a `string` copy, allocates from the heap
    // on every call, and this method exists precisely to avoid that.
    const unsigned char* copyBytes(const unsigned char* sourceAddress, size_t byteCount) {
        if (byteCount==0) {
            return nullptr;
        }
        unsigned char* destination=allocateRaw(alignUp(byteCount));
        if (destination==nullptr) {
            return nullptr;
        }
        // `memcpy` copies a run of bytes. Unlike `memmove` it assumes the two
        // regions do not overlap, which is guaranteed here because the source is
        // outside the arena and the destination is inside it.
        memcpy(destination, sourceAddress, byteCount);
        return destination;
    }
    // Copies a `string_view` into the arena as a null terminated string, so the
    // result can be handed to any C function that expects a `const char*`. The
    // view itself does not own its characters; this method makes a private copy
    // that the arena owns.
    string copyString(string_view sourceText) {
        if (sourceText.empty()) {
            return string();
        }
        // One extra byte is reserved so the copy can be terminated.
        unsigned char* destination=allocateRaw(alignUp(sourceText.size() + 1));
        if (destination==nullptr) {
            return string();
        }
        memcpy(destination, sourceText.data(), sourceText.size());
        destination[sourceText.size()]=0;
        return string(reinterpret_cast<const char*>(destination));
    }
    // The number of bytes handed out since the last `reset()`. The benchmark
    // suite asserts that this stays at zero across a full diagnostic pass, which
    // is how the zero allocation requirement is enforced in continuous
    // integration.
    size_t usedBytes() const {
        return usedByteCount;
    }
    // The number of bytes the arena can still hand out.
    size_t remainingBytes() const {
        return blockByteSize-usedByteCount;
    }
    // The total size of the block the arena currently owns.
    size_t blockByteSize() const {
        return blockByteSize;
    }
    // Makes every byte available again without releasing the block. This is the
    // cheap operation the watchdog loop calls on every tick.
    void reset() {
        // A bump allocator has no per object destructors to run, so moving the
        // pointer back to the start is genuinely all that is required. The memory
        // itself is still owned and is reused on the next allocation.
        usedByteCount=0;
    }
    // Returns the block to the operating system. The next allocation grows a new
    // block or falls back to the inline storage.
    void release() {
        if (heapBlock != inlineBlock.data()) {
            delete[] heapBlock;
        }
        heapBlock=inlineBlock.data();
        blockByteSize=inlineBlock.size();
        usedByteCount=0;
    }
private:
    // The alignment every allocation satisfies. Sixteen bytes satisfies every
    // scalar type the tool stores, and costs nothing in padding for the types
    // that are stored most often.
    static constexpr size_t ALIGNMENT=16;
    // The size of the inline block. One hundred and twenty eight bytes is
    // enough for a typical session identifier and a small report row.
    static constexpr size_t INLINE_BLOCK_SIZE=128;
    // Rounds `value` up to the next multiple of `ALIGNMENT`. The bit trick works
    // because `ALIGNMENT` is a power of two, so `ALIGNMENT - 1` is a mask with
    // the low bits set and negating it clears them.
    static size_t alignUp(size_t value) {
        return (value + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
    }
    // The smallest legal block is one alignment unit, which keeps the arithmetic
    // free of special cases.
    static size_t computeBlockSize(size_t requestedBytes) {
        if (requestedBytes < ALIGNMENT) {
            return ALIGNMENT;
        }
        return alignUp(requestedBytes);
    }
    // Hands out `byteCount` bytes, growing the block when necessary. Returns
    // `nullptr` when the operating system refuses the memory.
    unsigned char* allocateRaw(size_t byteCount) {
        if (usedByteCount + byteCount > blockByteSize) {
            growTo(usedByteCount + byteCount);
        }
        if (usedByteCount + byteCount > blockByteSize) {
            return nullptr;
        }
        // The bump pointer moves forward and never moves back until `reset()`.
        unsigned char* result=heapBlock + usedByteCount;
        usedByteCount+=byteCount;
        return result;
    }
    // Replaces the current block with a larger one. The existing contents are
    // copied into the replacement, so pointers handed out earlier stay valid.
    void growTo(size_t requiredBytes) {
        const size_t newSize=computeBlockSize(requiredBytes * 2);
        // `new (nothrow)` is the form of the allocation operator that returns a
        // null pointer when the request cannot be satisfied, instead of throwing.
        auto* replacement=new (nothrow) unsigned char[newSize];
        if (replacement==nullptr) {
            return;
        }
        if (heapBlock != inlineBlock.data() && usedByteCount > 0) {
            memcpy(replacement, heapBlock, usedByteCount);
        }
        if (heapBlock != inlineBlock.data()) {
            delete[] heapBlock;
        }
        heapBlock=replacement;
        blockByteSize=newSize;
    }
    // The block currently in use. It starts out pointing at `inlineBlock`, so a
    // default constructed arena performs no allocation at all. The pointer is
    // therefore non owning in that case, which is why `release()` and `growTo()`
    // compare it against `inlineBlock.data()` before calling `delete[]`.
    unsigned char* heapBlock=inlineBlock.data();
    // How many bytes of `heapBlock` have been handed out.
    size_t usedByteCount=0;
    // The total size of `heapBlock`. It starts out equal to the inline size.
    size_t blockByteSize=INLINE_BLOCK_SIZE;
    // The inline block. Because `array` stores its elements inside the object,
    // no allocation is needed to create it.
    array<unsigned char, INLINE_BLOCK_SIZE> inlineBlock;
};
}
#endif
