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
#include <utility>
#include <vector>

namespace PortPS5::GuestMemory {

// Half-open free extent [base, base + size), always inside the arena.
struct FreeExtent {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

// Address-ordered first-fit allocator over free extents, with ownership
// tracking over live allocations.
//
// The free tree is a treap keyed by base address, each node augmented with
// the largest free extent in its subtree (the Linux rb_subtree_gap technique
// from the spec, with a treap instead of an rbtree for compactness).
// Search prunes only subtrees whose maximum is smaller than the request,
// which is a universally necessary condition, so pruning can never skip a
// fitting extent; the exact aligned-fit check decides. This deliberately
// uses a weaker bound than the spec's bytes+align-16KiB hint, which is only
// safe when every extent base is 16 KiB-aligned AND the waste is maximal;
// see the implementation for the counterexample. Raised as spec question.
//
// Ownership: every successful Allocate records [base, size) in a live set
// (a second treap reusing the same node type); Free requires an exact match
// and removes it. A range the allocator never returned — including a
// sub-range of a live allocation — is rejected, so a caller bug fails loudly
// instead of aliasing live guest memory. (Fixed mappings from the future
// MarkUsed op record here too, so they free uniformly — wiring PR.)
class ExtentAllocator {
public:
    /// Starts without an arena; Init must succeed before allocating.
    ExtentAllocator() = default;
    /// Copying is disabled because each allocator exclusively owns its metadata.
    ExtentAllocator(const ExtentAllocator&) = delete;
    /// Copy assignment is disabled to prevent sharing owned metadata nodes.
    ExtentAllocator& operator=(const ExtentAllocator&) = delete;
    /// Releases allocator metadata without freeing or unmapping guest memory.
    ~ExtentAllocator();

    /// Covers [base, base + bytes) as one free extent, dropping prior state.
    /// Every failure mode (zero bytes, wrapping range, replacement-node
    /// allocation failure) returns false with the previous arena untouched.
    [[nodiscard]] bool Init(std::uint64_t base, std::uint64_t bytes) noexcept;

    /// Lowest-address fit for bytes at a nonzero power-of-two byte alignment.
    /// Returns 0 for zero bytes, invalid alignment, no fitting extent, or
    /// metadata allocation failure, leaving free and live ranges unchanged.
    /// Callers must use a nonzero arena base to make 0 an unambiguous failure
    /// sentinel: Init accepts base 0, and allocating there also returns 0.
    /// Success records the range in the live set (see class comment).
    [[nodiscard]] std::uint64_t Allocate(std::uint64_t bytes, std::uint64_t alignment) noexcept;

    /// Releases exactly the [base, base + bytes) range of a live allocation,
    /// coalescing with neighboring free extents and returning true.
    /// Any other range — unknown, a sub-range of a live allocation, a double
    /// free, out-of-arena or
    /// wrapping — returns false with the tree untouched, as does a
    /// replacement-node allocation failure.
    [[nodiscard]] bool Free(std::uint64_t base, std::uint64_t bytes) noexcept;

    /// True when [address, address + bytes) lies inside the arena bounds.
    /// Checks bounds regardless of whether the range is free or allocated.
    /// False before successful Init, for zero bytes, or for a wrapping range.
    [[nodiscard]] bool Contains(std::uint64_t address, std::uint64_t bytes) const noexcept;

    /// Number of disjoint free extents (introspection for tests).
    [[nodiscard]] std::uint64_t FreeExtentCount() const noexcept;

    /// Test-only introspection: calls visit(base, size) per free extent in
    /// ascending address order. Used by the differential test to compare
    /// against the reference model's free set. Not for hot paths.
    /// Reentrancy-safe: extents are snapshotted before the first visit, so a
    /// visitor may call back into the allocator (tests do) without crashing
    /// on nodes deleted mid-traversal; the visitor observes the snapshot.
    /// Snapshot growth errors (std::bad_alloc or std::length_error) propagate
    /// before any visits. Exceptions from visit propagate immediately,
    /// skipping the remaining extents.
    template <typename Fn>
    void ForEachFree(Fn visit) const {
        // Snapshot first (may throw bad_alloc on OOM); the traversal itself
        // then touches no live tree state.
        std::vector<FreeExtent> snapshot;
        ForEachFreeImpl(&snapshot, &AppendExtent);
        for (const auto& extent : snapshot) {
            visit(extent.base, extent.size);
        }
    }

private:
    struct Node;

    /// Appends to the `vector<FreeExtent>` at out; vector growth errors propagate.
    static void AppendExtent(void* out, std::uint64_t base, std::uint64_t size) {
        static_cast<std::vector<FreeExtent>*>(out)->push_back(FreeExtent{base, size});
    }

    using VisitorC = void (*)(void* fn, std::uint64_t base, std::uint64_t size);
    /// Visits the live free tree in address order, forwarding fn to visit.
    /// The callback must not mutate the tree; callback exceptions propagate.
    void ForEachFreeImpl(void* fn, VisitorC visit) const;

    // Treap primitives (private static members so they can name Node).

    /// Recomputes the subtree maximum for a non-null node after mutation.
    static void Refresh(Node* node) noexcept;
    /// Returns the merged root; every key in left must precede every key in right.
    static Node* Merge(Node* left, Node* right) noexcept;
    /// Partitions slot into left (base < key) and right (base >= key) trees.
    static void Split(Node* slot, std::uint64_t key, Node*& left, Node*& right) noexcept;
    /// Inserts an owned, detached node with a unique base, updating slot and maxima.
    static void InsertNode(Node*& slot, Node* node) noexcept;
    /// Erases the node with exactly key; returns the removed node or nullptr.
    static Node* EraseKey(Node*& slot, std::uint64_t key) noexcept;
    /// Returns the smallest-base node at or above key, or nullptr if none exists.
    static Node* LowerBound(Node* slot, std::uint64_t key) noexcept;
    /// Returns the greatest-base node below key, or nullptr if none exists.
    static Node* Predecessor(Node* slot, std::uint64_t key) noexcept;
    /// Exact-key lookup; returns the node or nullptr.
    static Node* Find(Node* slot, std::uint64_t key) noexcept;
    /// Deletes all metadata in slot; nullptr is allowed and guest memory is untouched.
    static void Destroy(Node* slot) noexcept;
    /// Visits slot in address order, forwarding fn; no mutation is allowed and exceptions propagate.
    static void InOrder(const Node* slot, void* fn, VisitorC visit);

    Node* root_ = nullptr;      // free extents, keyed by base
    Node* liveRoot_ = nullptr;  // live allocations, keyed by base
    std::uint64_t arenaBase_ = 0;
    std::uint64_t arenaEnd_ = 0;
    std::uint64_t priorityCounter_ = 0;
};

}  // namespace PortPS5::GuestMemory

#endif
