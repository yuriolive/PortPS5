// Optimization/src/SharedMemoryBarrierInserter.cpp
// In wave64 compute shaders on RDNA2 / GFX10.3, workgroup-shared (LDS) writes require
// wave-LDS barriers so writes from one subgroup are ordered before subsequent reads.
// Also implements atomic-zero elimination: skips SharedAtomicIAdd32 when the value is immediate 0.
#include "Optimization/SharedMemoryBarrierInserter.hpp"
#include "IntermediateRepresentation/IrOpcode.hpp"
#include <vector>

namespace ShaderRecompiler {

SharedMemoryBarrierStats SharedMemoryBarrierInserter::Insert(IrProgram& program, std::uint32_t waveSize) const {
    SharedMemoryBarrierStats stats;

    // Scan all blocks in the program
    for (const auto& blockPtr : program.Blocks()) {
        if (!blockPtr) {
            continue;
        }
        IrBlock& block = *blockPtr;

        // Collect instructions first to avoid iterator invalidation during mutation
        std::vector<IrValue*> insts(block.Instructions().begin(), block.Instructions().end());

        for (std::size_t i = 0; i < insts.size(); ++i) {
            IrValue* inst = insts[i];
            if (!inst) {
                continue;
            }

            // Peephole: Atomic-zero skip
            // Skip or eliminate SharedAtomicIAdd32 when the addend operand is immediate 0
            if (inst->Opcode() == IrOpcode::SharedAtomicIAdd32 && inst->ArgumentCount() >= 2) {
                IrValue* operand = inst->Argument(1)->Resolve();
                if (operand && operand->HasImmediate() && operand->ImmediateU32() == 0u) {
                    if (!inst->HasUses()) {
                        // Unused atomic result: replace opcode with Identity/no-op
                        inst->ReplaceOpcode(IrOpcode::Identity);
                    }
                }
            }

            // WaveLdsScope ordering barrier: insert IrOpcode::Barrier after LDS writes when waveSize == 64
            const SharedAccess access = SharedAccessOf(inst->Opcode());
            if (waveSize == 64u && (access == SharedAccess::Write || access == SharedAccess::Atomic)) {
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

    return stats;
}

}

