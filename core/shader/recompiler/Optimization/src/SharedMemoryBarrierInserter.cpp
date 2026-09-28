// Optimization/src/SharedMemoryBarrierInserter.cpp
// In wave64 compute shaders on RDNA2 / GFX10.3, workgroup-shared (LDS) writes require
// wave-LDS barriers so writes from one subgroup are ordered before subsequent reads.
// Barriers are inserted in uniform blocks or placed at uniform reconvergence points (merge blocks)
// to prevent deadlocks in lane-divergent control flow.
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

    return stats;
}

}
