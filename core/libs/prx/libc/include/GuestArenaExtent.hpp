// core/libs/prx/libc/include/GuestArenaExtent.hpp
// Augmented extent-tree first-fit allocator for the guest arena
// (docs/spec/guest-memory.md, Target design "Arena allocator").
//
// Subsystem: guest-memory (libc). Host-only helper: no guest-called exports,
// no guest ABI attribute, no throws. Threading: single-threaded; the caller
// holds its lock across Init/Allocate/Free. Allocation order is part of the
// guest-visible contract (titles index tables by absolute address), so
// Allocate must return exactly what the reference ascending linear first-fit
// scan returns (upstream GuestArena::Allocate over the used-range map).

#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GUESTARENAEXTENT_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GUESTARENAEXTENT_HPP

#include <cstdint>

namespace PortPS5::GuestMemory {

// Half-open free extent [base, base + size), always inside the arena.
struct FreeExtent {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

// Address-ordered first-fit allocator over free extents.
//
// The tree is a treap keyed by base address, each node augmented with the
// largest free extent in its subtree (the Linux rb_subtree_gap technique
// from the spec, with a treap instead of an rbtree for compactness).
// Search prunes only subtrees whose maximum is smaller than the request,
// which is a universally necessary condition, so pruning can never skip a
// fitting extent; the exact aligned-fit check decides. This deliberately
// uses a weaker bound than the spec's bytes+align-16KiB hint, which is only
// safe when every extent base is 16 KiB-aligned AND the waste is maximal;
// see the implementation for the counterexample. Raised as spec question.
class ExtentAllocator {
public:
    ExtentAllocator() = default;
    ExtentAllocator(const ExtentAllocator&) = delete;
    ExtentAllocator& operator=(const ExtentAllocator&) = delete;
    ~ExtentAllocator();

    // Covers [base, base + bytes) as one free extent, dropping prior state.
    // Every failure mode (zero bytes, wrapping range, replacement-node
    // allocation failure) returns false with the previous arena untouched.
    [[nodiscard]] bool Init(std::uint64_t base, std::uint64_t bytes) noexcept;

    // Lowest-address fit for bytes at a pow2 alignment, or 0 on failure.
    // 0 is never a valid guest address (arena lives above 1 TiB), so it is
    // an unambiguous failure sentinel. bytes == 0 or non-pow2 align fails.
    [[nodiscard]] std::uint64_t Allocate(std::uint64_t bytes, std::uint64_t alignment) noexcept;

    // Returns [base, base + bytes) to the free set, coalescing neighbours.
    // Every failure mode (unknown range, overlap with a free extent such as
    // a double free, out-of-arena or wrapping range, replacement-node
    // allocation failure) returns false with the tree untouched.
    [[nodiscard]] bool Free(std::uint64_t base, std::uint64_t bytes) noexcept;

    // True when [address, address + bytes) lies inside the arena bounds.
    [[nodiscard]] bool Contains(std::uint64_t address, std::uint64_t bytes) const noexcept;

    // Number of disjoint free extents (introspection for tests).
    [[nodiscard]] std::uint64_t FreeExtentCount() const noexcept;

    // Test-only introspection: calls visit(base, size) per free extent in
    // ascending address order. Used by the differential test to compare
    // against the reference model's free set. Not for hot paths.
    template <typename Fn>
    void ForEachFree(Fn visit) const {
        ForEachFreeImpl(&visit, &CallVisitor<Fn>);
    }

private:
    struct Node;

    template <typename Fn>
    static void CallVisitor(void* fn, std::uint64_t base, std::uint64_t size) {
        (*static_cast<Fn*>(fn))(base, size);
    }
    using VisitorC = void (*)(void* fn, std::uint64_t base, std::uint64_t size);
    void ForEachFreeImpl(void* fn, VisitorC visit) const;

    // Treap primitives (private static members so they can name Node).
    static void Refresh(Node* node) noexcept;
    static Node* Merge(Node* left, Node* right) noexcept;
    static void Split(Node* slot, std::uint64_t key, Node*& left, Node*& right) noexcept;
    static void InsertNode(Node*& slot, Node* node) noexcept;
    // Erases the node with exactly key; returns the removed node or nullptr.
    static Node* EraseKey(Node*& slot, std::uint64_t key) noexcept;
    static Node* LowerBound(Node* slot, std::uint64_t key) noexcept;
    static Node* Predecessor(Node* slot, std::uint64_t key) noexcept;
    static void Destroy(Node* slot) noexcept;
    static void InOrder(const Node* slot, void* fn, VisitorC visit);

    Node* root_ = nullptr;
    std::uint64_t arenaBase_ = 0;
    std::uint64_t arenaEnd_ = 0;
    std::uint64_t priorityCounter_ = 0;
};

}  // namespace PortPS5::GuestMemory

#endif
