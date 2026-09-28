// core/libs/prx/libc/src/GuestArenaExtent.cpp
// Treap implementation of the arena extent allocator declared in
// prx/libc/include/GuestArenaExtent.hpp (docs/spec/guest-memory.md).
//
// Why a treap: the spec asks for an address-ordered balanced tree augmented
// with the subtree-maximum free size. A treap gives expected O(log n) with
// ~100 lines (no red-black cases), and deterministic splitmix64 priorities
// keep allocation order reproducible across runs, which the differential
// test relies on. Nodes come from plain new(nothrow); allocation metadata is
// bounded (thousands of extents), never per guest malloc.
//
// Pruning safety note: search prunes a subtree only when its recorded
// maximum free size is smaller than the requested bytes. A fitting extent is
// at least as large as the request, so this necessary condition never skips
// a fit regardless of base alignment. The spec's tighter bytes+align-16KiB
// bound is NOT used: it can skip a fitting extent (e.g. a 32 KiB extent at a
// 64 KiB-aligned base serving a 16 KiB request at 64 KiB alignment, where the
// bound demands 64 KiB). The exact alignUp fit check at each candidate keeps
// results identical to the reference linear scan.

#include "prx/libc/include/GuestArenaExtent.hpp"

#include <limits>
#include <new>

namespace PortPS5::GuestMemory {
namespace {

// True when value is a non-zero power of two.
bool IsPow2(std::uint64_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

// False when size is 0 or base + size overflows.
bool RangeValid(std::uint64_t base, std::uint64_t size) noexcept {
    return size != 0 && size <= std::numeric_limits<std::uint64_t>::max() - base;
}

// Deterministic 64-bit mixer for treap priorities (splitmix64, fixed key).
// Determinism matters: the same op stream must rebuild the same shape so
// allocation addresses are reproducible run to run.
std::uint64_t MixPriority(std::uint64_t counter) noexcept {
    std::uint64_t z = counter + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

}  // namespace

struct ExtentAllocator::Node {
    std::uint64_t base;
    std::uint64_t size;
    std::uint64_t maxSub;  // largest free extent in this subtree, incl. self
    std::uint64_t prio;
    Node* left = nullptr;
    Node* right = nullptr;
};

// Recomputes the augmentation after a structural change.
void ExtentAllocator::Refresh(Node* node) noexcept {
    std::uint64_t best = node->size;
    if (node->left != nullptr && node->left->maxSub > best) {
        best = node->left->maxSub;
    }
    if (node->right != nullptr && node->right->maxSub > best) {
        best = node->right->maxSub;
    }
    node->maxSub = best;
}

// Treap merge of two BSTs where every key in left < every key in right.
ExtentAllocator::Node* ExtentAllocator::Merge(Node* left, Node* right) noexcept {
    if (left == nullptr) {
        return right;
    }
    if (right == nullptr) {
        return left;
    }
    if (left->prio < right->prio) {
        left->right = Merge(left->right, right);
        Refresh(left);
        return left;
    }
    right->left = Merge(left, right->left);
    Refresh(right);
    return right;
}

// Splits slot by key: left gets bases < key, right gets bases >= key.
void ExtentAllocator::Split(Node* slot, std::uint64_t key, Node*& left, Node*& right) noexcept {
    if (slot == nullptr) {
        left = right = nullptr;
        return;
    }
    if (key <= slot->base) {
        Split(slot->left, key, left, slot->left);
        right = slot;
        Refresh(right);
    } else {
        Split(slot->right, key, slot->right, right);
        left = slot;
        Refresh(left);
    }
}

// Inserts an already-allocated node by base address, preserving heap order.
void ExtentAllocator::InsertNode(Node*& slot, Node* node) noexcept {
    if (slot == nullptr) {
        slot = node;
        return;
    }
    if (node->prio < slot->prio) {
        Split(slot, node->base, node->left, node->right);
        slot = node;
        Refresh(slot);
        return;
    }
    if (node->base < slot->base) {
        InsertNode(slot->left, node);
    } else {
        InsertNode(slot->right, node);
    }
    Refresh(slot);
}

ExtentAllocator::Node* ExtentAllocator::EraseKey(Node*& slot, std::uint64_t key) noexcept {
    if (slot == nullptr) {
        return nullptr;
    }
    if (key == slot->base) {
        Node* removed = slot;
        slot = Merge(slot->left, slot->right);
        removed->left = removed->right = nullptr;
        return removed;
    }
    Node* removed = (key < slot->base) ? EraseKey(slot->left, key) : EraseKey(slot->right, key);
    if (removed != nullptr) {
        Refresh(slot);
    }
    return removed;
}

// Smallest node with base >= key, or nullptr.
ExtentAllocator::Node* ExtentAllocator::LowerBound(Node* slot, std::uint64_t key) noexcept {
    Node* best = nullptr;
    while (slot != nullptr) {
        if (slot->base >= key) {
            best = slot;
            slot = slot->left;
        } else {
            slot = slot->right;
        }
    }
    return best;
}

// Greatest node with base < key, or nullptr.
ExtentAllocator::Node* ExtentAllocator::Predecessor(Node* slot, std::uint64_t key) noexcept {
    Node* best = nullptr;
    while (slot != nullptr) {
        if (slot->base < key) {
            best = slot;
            slot = slot->right;
        } else {
            slot = slot->left;
        }
    }
    return best;
}

void ExtentAllocator::Destroy(Node* slot) noexcept {
    if (slot == nullptr) {
        return;
    }
    Destroy(slot->left);
    Destroy(slot->right);
    delete slot;
}

void ExtentAllocator::InOrder(const Node* slot, void* fn, VisitorC visit) {
    if (slot == nullptr) {
        return;
    }
    InOrder(slot->left, fn, visit);
    visit(fn, slot->base, slot->size);
    InOrder(slot->right, fn, visit);
}

ExtentAllocator::~ExtentAllocator() {
    Destroy(root_);
    root_ = nullptr;
}

bool ExtentAllocator::Init(std::uint64_t base, std::uint64_t bytes) noexcept {
    if (!RangeValid(base, bytes)) {
        return false;
    }
    Destroy(root_);
    priorityCounter_ = 0;  // restart the deterministic priority stream
    Node* extent = new (std::nothrow) Node{base, bytes, bytes, MixPriority(++priorityCounter_), nullptr, nullptr};
    if (extent == nullptr) {
        root_ = nullptr;
        return false;
    }
    // Bounds are published only once the tree is non-empty, so Contains can
    // tell an uninitialized (or OOM-failed) allocator from an empty arena by
    // testing arenaEnd_ <= arenaBase_.
    arenaBase_ = base;
    arenaEnd_ = base + bytes;
    root_ = extent;
    return true;
}

bool ExtentAllocator::Contains(std::uint64_t address, std::uint64_t bytes) const noexcept {
    if (!RangeValid(address, bytes)) {
        return false;
    }
    if (arenaEnd_ <= arenaBase_) {
        return false;  // uninitialized: Init never succeeded
    }
    // Overflow-safe containment: RangeValid rules out wrap-around, so both
    // differences below are exact.
    return address >= arenaBase_ && address - arenaBase_ <= (arenaEnd_ - arenaBase_) - bytes;
}

std::uint64_t ExtentAllocator::FreeExtentCount() const noexcept {
    std::uint64_t count = 0;
    ForEachFree([&](std::uint64_t, std::uint64_t) { ++count; });
    return count;
}

void ExtentAllocator::ForEachFreeImpl(void* fn, VisitorC visit) const {
    InOrder(root_, fn, visit);
}

std::uint64_t ExtentAllocator::Allocate(std::uint64_t bytes, std::uint64_t alignment) noexcept {
    if (bytes == 0 || !IsPow2(alignment)) {
        return 0;
    }
    // Address-ordered first-fit with safe pruning. At each node: descend
    // left when the left subtree may hold a fit (its maximum reaching the
    // request size is necessary for any fit inside), else try this extent
    // exactly, else go right. The recursion depth is the treap height
    // (expected logarithmic; arena metadata holds thousands of extents).
    // Local struct inside a member function: it sees private Node.
    struct Search {
        static bool Run(Node*& slot, std::uint64_t bytes, std::uint64_t alignment, std::uint64_t& prio,
                        std::uint64_t& out) noexcept {
            Node* node = slot;
            if (node == nullptr || node->maxSub < bytes) {
                return false;
            }
            if (node->left != nullptr && node->left->maxSub >= bytes) {
                if (Run(node->left, bytes, alignment, prio, out)) {
                    Refresh(node);
                    return true;
                }
            }
            // Exact check: alignUp can overflow near 2^64, in which case this
            // extent simply cannot serve the request and search continues.
            const std::uint64_t mask = alignment - 1;
            if (node->base <= std::numeric_limits<std::uint64_t>::max() - mask) {
                const std::uint64_t aligned = (node->base + mask) & ~mask;
                const std::uint64_t pad = aligned - node->base;
                if (pad <= node->size && bytes <= node->size - pad) {
                    out = aligned;
                    // Carve [aligned, aligned + bytes) out, reinserting the
                    // prefix/suffix remainders as free extents.
                    const std::uint64_t extentEnd = node->base + node->size;
                    const std::uint64_t allocEnd = aligned + bytes;
                    const std::uint64_t prefixBase = node->base;
                    const std::uint64_t prefixSize = pad;
                    const bool hasSuffix = allocEnd < extentEnd;
                    const std::uint64_t suffixBase = allocEnd;
                    const std::uint64_t suffixSize = extentEnd - allocEnd;
                    Node* merged = Merge(node->left, node->right);
                    delete node;
                    slot = merged;
                    // Remainder insertion uses fresh nodes; a nothrow failure
                    // here would drop a free range, so the carve is reported
                    // only when bookkeeping succeeded.
                    bool ok = true;
                    if (prefixSize != 0) {
                        Node* pre = new (std::nothrow) Node{prefixBase, prefixSize, prefixSize, MixPriority(++prio)};
                        if (pre == nullptr) {
                            ok = false;
                        } else {
                            InsertNode(slot, pre);
                        }
                    }
                    if (hasSuffix) {
                        Node* suf = new (std::nothrow) Node{suffixBase, suffixSize, suffixSize, MixPriority(++prio)};
                        if (suf == nullptr) {
                            ok = false;
                        } else {
                            InsertNode(slot, suf);
                        }
                    }
                    return ok;
                }
            }
            if (node->right != nullptr && node->right->maxSub >= bytes) {
                if (Run(node->right, bytes, alignment, prio, out)) {
                    Refresh(node);
                    return true;
                }
            }
            return false;
        }
    };

    std::uint64_t out = 0;
    if (!Search::Run(root_, bytes, alignment, priorityCounter_, out)) {
        return 0;
    }
    return out;
}

bool ExtentAllocator::Free(std::uint64_t base, std::uint64_t bytes) noexcept {
    if (!RangeValid(base, bytes) || !Contains(base, bytes)) {
        return false;
    }
    const std::uint64_t end = base + bytes;
    Node* prev = Predecessor(root_, base);
    Node* next = LowerBound(root_, base);
    if (next != nullptr && next->base == base) {
        return false;  // exact-base free extent exists: double free
    }
    if (prev != nullptr && prev->base + prev->size > base) {
        return false;  // overlaps a free extent on the left
    }
    if (next != nullptr && end > next->base) {
        return false;  // overlaps a free extent on the right
    }
    std::uint64_t newBase = base;
    std::uint64_t newSize = bytes;
    // Coalesce left: swallow the predecessor and continue with its base.
    if (prev != nullptr && prev->base + prev->size == base) {
        newBase = prev->base;
        newSize += prev->size;
        Node* removed = EraseKey(root_, prev->base);
        delete removed;
        next = LowerBound(root_, newBase);
    }
    // Coalesce right: swallow the successor.
    if (next != nullptr && newBase + newSize == next->base) {
        newSize += next->size;
        Node* removed = EraseKey(root_, next->base);
        delete removed;
    }
    Node* node = new (std::nothrow) Node{newBase, newSize, newSize, MixPriority(++priorityCounter_)};
    if (node == nullptr) {
        return false;
    }
    InsertNode(root_, node);
    return true;
}

}  // namespace PortPS5::GuestMemory
