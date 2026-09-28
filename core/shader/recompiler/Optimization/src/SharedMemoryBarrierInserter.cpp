// Optimization/src/SharedMemoryBarrierInserter.cpp
// In wave64 compute shaders on RDNA2 / GFX10.3, workgroup-shared (LDS) writes require
// wave-LDS barriers so writes from one subgroup are ordered before subsequent reads.
// Barriers are inserted in uniform blocks or placed at uniform reconvergence points (merge blocks)
// to prevent deadlocks in lane-divergent control flow.
//
// Ordering model (all placements are dynamically uniform, so no OpControlBarrier deadlock):
//   - Divergent LDS write  -> barrier at the start of the region's merge block.
//     Orders the write before every post-reconvergence LDS read.
//   - Divergent LDS read   -> barrier at the end of the region's outermost uniform header
//     (the block holding the divergent branch, which every lane executes before diverging).
//     Orders all pre-region LDS writes before the divergent read. A merge-block barrier
//     alone would execute AFTER the divergent read and provide no ordering for it.
// Limitation: a divergent write followed by a divergent read inside the SAME region with no
// intervening uniform point cannot be ordered by any workgroup barrier (no uniform point
// exists between them); cross-lane communication there requires uniform control flow, which
// is the M3 structurizer's scope (see docs/spec/shader-recompiler.md open questions).
#include "Optimization/SharedMemoryBarrierInserter.hpp"
#include "IntermediateRepresentation/IrBlock.hpp"
#include "IntermediateRepresentation/IrOpcode.hpp"
#include "IntermediateRepresentation/IrProgram.hpp"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ShaderRecompiler {

namespace {

bool IsDivergentBranchCondition(BranchCondition cond) {
    switch (cond) {
    case BranchCondition::VccZero:
    case BranchCondition::VccNonZero:
    case BranchCondition::ExecZero:
    case BranchCondition::ExecNonZero:
    case BranchCondition::SccZero:
    case BranchCondition::SccNonZero:
    case BranchCondition::Unknown:
        return true;
    default:
        return false;
    }
}

} // namespace

SharedMemoryBarrierStats SharedMemoryBarrierInserter::Insert(IrProgram& program, std::uint32_t waveSize) const {
    SharedMemoryBarrierStats stats;
    if (waveSize != 64u) {
        return stats;
    }

    const auto& blockInfoList = program.Metadata().blockInfo;
    const auto& blockOrder = program.BlockOrder();

    // Build CFG block id -> IrBlock* mapping using blockInfo if available, otherwise IrBlock::Id()
    std::unordered_map<std::uint32_t, IrBlock*> blockMap;
    std::unordered_map<const IrBlock*, std::uint32_t> blockToCfgId;

    if (!blockInfoList.empty() && blockOrder.size() == blockInfoList.size()) {
        for (std::size_t i = 0; i < blockOrder.size(); ++i) {
            if (blockOrder[i]) {
                const std::uint32_t cfgId = blockInfoList[i].id;
                blockMap[cfgId] = blockOrder[i];
                blockToCfgId[blockOrder[i]] = cfgId;
            }
        }
    } else {
        for (const auto& blockPtr : program.Blocks()) {
            if (blockPtr) {
                blockMap[blockPtr->Id()] = blockPtr.get();
                blockToCfgId[blockPtr.get()] = blockPtr->Id();
            }
        }
    }

    // Analyze divergence from metadata if available
    std::unordered_map<std::uint32_t, std::uint32_t> divergentToMergeBlock;
    // Direct header of each divergent block: the CFG id of the block holding the divergent
    // branch whose region contains the key block (innermost region wins for nesting).
    std::unordered_map<std::uint32_t, std::uint32_t> divergentToHeader;
    if (!blockInfoList.empty()) {
        std::unordered_map<std::uint32_t, const BlockInfo*> infoMap;
        for (const auto& info : blockInfoList) {
            infoMap[info.id] = &info;
        }

        for (const auto& info : blockInfoList) {
            if (info.terminator.kind == TerminatorKind::ConditionalBranch &&
                info.terminator.mergeBlock != InvalidControlFlowId &&
                IsDivergentBranchCondition(info.terminator.condition)) {
                const std::uint32_t mergeId = info.terminator.mergeBlock;

                // Traverse blocks in the divergent region until mergeBlock
                std::queue<std::uint32_t> worklist;
                if (info.terminator.trueBlock != InvalidControlFlowId && info.terminator.trueBlock != mergeId) {
                    worklist.push(info.terminator.trueBlock);
                }
                if (info.terminator.falseBlock != InvalidControlFlowId && info.terminator.falseBlock != mergeId) {
                    worklist.push(info.terminator.falseBlock);
                }

                std::unordered_set<std::uint32_t> visited;
                while (!worklist.empty()) {
                    const std::uint32_t curr = worklist.front();
                    worklist.pop();

                    if (curr == InvalidControlFlowId || curr == mergeId) {
                        continue;
                    }

                    // Nested divergent regions: inner branches overwrite outer merge mapping
                    // so that LDS writes are synchronized at the nearest/innermost reconvergence.
                    divergentToMergeBlock[curr] = mergeId;
                    divergentToHeader[curr] = info.id;

                    if (visited.insert(curr).second) {
                        auto it = infoMap.find(curr);
                        if (it != infoMap.end()) {
                            const auto& succInfo = *it->second;
                            if (succInfo.terminator.trueBlock != InvalidControlFlowId && succInfo.terminator.trueBlock != mergeId) {
                                worklist.push(succInfo.terminator.trueBlock);
                            }
                            if (succInfo.terminator.falseBlock != InvalidControlFlowId && succInfo.terminator.falseBlock != mergeId) {
                                worklist.push(succInfo.terminator.falseBlock);
                            }
                            for (const std::uint32_t target : succInfo.terminator.indirectTargets) {
                                if (target != InvalidControlFlowId && target != mergeId) {
                                    worklist.push(target);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::unordered_set<std::uint32_t> reconvergenceNeedingBarrier;
    // Headers needing a pre-region barrier: every lane executes the divergent header uniformly
    // before diverging, so a barrier at its end is a safe uniform point that executes BEFORE
    // any divergent LDS read in the region (ordering pre-region writes before the read).
    std::unordered_set<std::uint32_t> headersNeedingBarrier;

    // Walk from a divergent block outward through nested regions to the outermost header whose
    // own block is uniform. Returns InvalidControlFlowId when no safe uniform header exists
    // (e.g. self-loop region); callers then skip the pre-read barrier (documented limitation).
    const auto findOutermostUniformHeader = [&](std::uint32_t divCfgId) -> std::uint32_t {
        std::unordered_set<std::uint32_t> seen;
        std::uint32_t cursor = divCfgId;
        while (divergentToHeader.contains(cursor) && seen.insert(cursor).second) {
            const std::uint32_t candidate = divergentToHeader[cursor];
            if (candidate == InvalidControlFlowId) {
                return InvalidControlFlowId;
            }
            // Header inside another divergent region: keep walking outward.
            if (divergentToMergeBlock.contains(candidate)) {
                cursor = candidate;
                continue;
            }
            return candidate;
        }
        return InvalidControlFlowId;
    };

    // Scan all blocks in the program
    const std::size_t totalBlocks = blockOrder.empty() ? program.Blocks().size() : blockOrder.size();
    for (std::size_t bIdx = 0; bIdx < totalBlocks; ++bIdx) {
        IrBlock* blockPtr = blockOrder.empty() ? program.Blocks()[bIdx].get() : blockOrder[bIdx];
        if (!blockPtr) {
            continue;
        }
        IrBlock& block = *blockPtr;
        const auto cfgIt = blockToCfgId.find(&block);
        const std::uint32_t blockCfgId = (cfgIt != blockToCfgId.end()) ? cfgIt->second : block.Id();

        const auto divIt = divergentToMergeBlock.find(blockCfgId);
        const bool isDivergent = (divIt != divergentToMergeBlock.end());

        // Collect instructions first to avoid iterator invalidation during mutation
        std::vector<IrValue*> insts(block.Instructions().begin(), block.Instructions().end());

        for (std::size_t i = 0; i < insts.size(); ++i) {
            IrValue* inst = insts[i];
            if (!inst) {
                continue;
            }

            const SharedAccess access = SharedAccessOf(inst->Opcode());
            if (access == SharedAccess::Write || access == SharedAccess::Atomic ||
                access == SharedAccess::Append || access == SharedAccess::Consume) {
                if (isDivergent) {
                    // In a lane-divergent block, placing a workgroup barrier directly causes GPU deadlocks.
                    // Place the barrier at the uniform reconvergence point (merge block) instead.
                    const std::uint32_t mergeId = divIt->second;
                    if (mergeId != InvalidControlFlowId && blockMap.contains(mergeId)) {
                        reconvergenceNeedingBarrier.insert(mergeId);
                    }
                } else {
                    // Uniform block: insert IrOpcode::Barrier directly after the LDS access
                    IrValue& barrier = program.CreateValue(IrOpcode::Barrier, IrType::Void);
                    if (i + 1 < insts.size()) {
                        block.InsertInstructionBefore(insts[i + 1], &barrier);
                    } else {
                        block.AppendInstruction(&barrier);
                    }
                    ++stats.insertedBarriers;
                }
            } else if (access == SharedAccess::Read) {
                // An LDS read inside a divergent region needs synchronization BEFORE it executes:
                // a merge-block barrier would run after the read and order nothing for it.
                // Request a barrier at the end of the outermost uniform header dominating the
                // read instead; it executes uniformly before the region diverges.
                if (isDivergent) {
                    const std::uint32_t headerId = findOutermostUniformHeader(blockCfgId);
                    if (headerId != InvalidControlFlowId && blockMap.contains(headerId)) {
                        headersNeedingBarrier.insert(headerId);
                    }
                }
            }
        }
    }

    // Insert barriers at the beginning of reconvergence merge blocks (after Phis)
    for (const std::uint32_t mergeId : reconvergenceNeedingBarrier) {
        auto it = blockMap.find(mergeId);
        if (it == blockMap.end() || !it->second) {
            continue;
        }
        IrBlock& mergeBlock = *it->second;

        // Check if mergeBlock already starts with a Barrier
        bool hasBarrier = false;
        IrValue* insertBefore = nullptr;
        for (IrValue* val : mergeBlock.Instructions()) {
            if (!val) continue;
            if (val->Opcode() == IrOpcode::Phi) {
                continue;
            }
            if (val->Opcode() == IrOpcode::Barrier) {
                hasBarrier = true;
            }
            insertBefore = val;
            break;
        }

        if (!hasBarrier) {
            IrValue& barrier = program.CreateValue(IrOpcode::Barrier, IrType::Void);
            if (insertBefore) {
                mergeBlock.InsertInstructionBefore(insertBefore, &barrier);
            } else {
                mergeBlock.AppendInstruction(&barrier);
            }
            ++stats.insertedBarriers;
        }
    }

    // Insert barriers at the end of uniform headers preceding divergent regions that contain
    // LDS reads. Appending puts the barrier after the header's existing instructions but still
    // before any successor executes; the header runs uniformly (lanes diverge only at its
    // branch), so the barrier is dynamically uniform and orders pre-region LDS writes before
    // the divergent read. A header that already holds a Barrier (e.g. after a uniform LDS
    // write) needs no extra one: that barrier already executes before the region.
    for (const std::uint32_t headerId : headersNeedingBarrier) {
        auto it = blockMap.find(headerId);
        if (it == blockMap.end() || !it->second) {
            continue;
        }
        IrBlock& headerBlock = *it->second;

        bool hasBarrier = false;
        for (IrValue* val : headerBlock.Instructions()) {
            if (val && val->Opcode() == IrOpcode::Barrier) {
                hasBarrier = true;
                break;
            }
        }

        if (!hasBarrier) {
            IrValue& barrier = program.CreateValue(IrOpcode::Barrier, IrType::Void);
            headerBlock.AppendInstruction(&barrier);
            ++stats.insertedBarriers;
        }
    }

    return stats;
}

}
