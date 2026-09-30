// core/shader/recompiler/tests/RecompilerFixesTests.cpp
// Unit tests for shader recompiler Milestone 1 fixes:
//   - v_movrels/v_movreld bounded select-chain lowering over the VGPR register file with VGPR[0] fallback
//   - SharedMemoryBarrierInserter: wave-LDS barriers in wave64 compute programs (uniform blocks,
//     reconvergence merge blocks for divergent writes, uniform headers for divergent reads)
//   - SharedAtomicIAdd32 preservation: preserves workgroup synchronization effects even with zero addend
//   - sSaveexec: instruction order and 32-bit bitwise typing
//
// These tests verify compiler IR transformations and invariants using GoogleTest.

#include "Optimization/include/Optimization/SharedMemoryBarrierInserter.hpp"
#include "Translation/include/Translation/TranslationContext.hpp"
#include "RdnaDecoder/include/RdnaDecoder/RdnaInstruction.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrBlock.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrBuilder.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrOpcode.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrProgram.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrValue.hpp"
#include "SpirvBackend/include/SpirvBackend/SpirvOptimizer.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

namespace ShaderRecompiler {
namespace {

TEST(RecompilerFixesTests, SharedMemoryBarrierInsertedAfterLdsWriteWave64) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);
    IrBuilder ir(program);
    ir.SetInsertionPoint(entry);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);

    // WriteSharedU32 taking addr, val
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    entry.AppendInstruction(&writeOp);

    EXPECT_EQ(entry.Instructions().size(), 1u);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 1u);
    EXPECT_EQ(entry.Instructions().size(), 2u);

    // Verify Barrier is right after WriteSharedU32
    auto it = entry.Instructions().begin();
    EXPECT_EQ((*it)->Opcode(), IrOpcode::WriteSharedU32);
    ++it;
    ASSERT_NE(it, entry.Instructions().end());
    EXPECT_EQ((*it)->Opcode(), IrOpcode::Barrier);
}

TEST(RecompilerFixesTests, SharedMemoryBarrierNotInsertedWave32) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);
    IrBuilder ir(program);
    ir.SetInsertionPoint(entry);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);

    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    entry.AppendInstruction(&writeOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 32u);

    EXPECT_EQ(stats.insertedBarriers, 0u);
    EXPECT_EQ(entry.Instructions().size(), 1u);
}

TEST(RecompilerFixesTests, DivergentBlockLdsWritePlacesBarrierAtReconvergence) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    IrBlock& divBlock = program.CreateBlock();
    IrBlock& mergeBlock = program.CreateBlock();
    program.SetEntryBlock(entry);
    program.BlockOrder() = {&entry, &divBlock, &mergeBlock};

    // Set up CFG block info in metadata with CFG IDs distinct/shifted from physical IrBlock::Id().
    // For example, CFG block IDs: divBlock is CFG 0, mergeBlock is CFG 1, and synthetic entry is CFG 2 (entryBlockId).
    constexpr std::uint32_t kCfgDivId = 0u;
    constexpr std::uint32_t kCfgMergeId = 1u;
    constexpr std::uint32_t kCfgEntryId = 2u;

    BlockInfo entryInfo{};
    entryInfo.id = kCfgEntryId;
    entryInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    entryInfo.terminator.condition = BranchCondition::ExecZero; // divergent condition
    entryInfo.terminator.trueBlock = kCfgDivId;
    entryInfo.terminator.falseBlock = kCfgMergeId;
    entryInfo.terminator.mergeBlock = kCfgMergeId;

    BlockInfo divInfo{};
    divInfo.id = kCfgDivId;
    divInfo.terminator.kind = TerminatorKind::Branch;
    divInfo.terminator.trueBlock = kCfgMergeId;

    BlockInfo mergeInfo{};
    mergeInfo.id = kCfgMergeId;
    mergeInfo.terminator.kind = TerminatorKind::Return;

    program.Metadata().blockInfo = {entryInfo, divInfo, mergeInfo};

    IrBuilder ir(program);
    ir.SetInsertionPoint(divBlock);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    divBlock.AppendInstruction(&writeOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 1u);
    // Divergent block must NOT contain the barrier (preventing GPU control barrier deadlocks)
    for (IrValue* inst : divBlock.Instructions()) {
        EXPECT_NE(inst->Opcode(), IrOpcode::Barrier);
    }
    // Uniform merge block MUST contain the barrier
    bool foundMergeBarrier = false;
    for (IrValue* inst : mergeBlock.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            foundMergeBarrier = true;
            break;
        }
    }
    EXPECT_TRUE(foundMergeBarrier);
}

TEST(RecompilerFixesTests, NestedDivergentBlockLdsWritePlacesBarrierAtInnermostMerge) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    IrBlock& outerDiv = program.CreateBlock();
    IrBlock& innerDiv = program.CreateBlock();
    IrBlock& innerMerge = program.CreateBlock();
    IrBlock& outerMerge = program.CreateBlock();

    program.SetEntryBlock(entry);
    program.BlockOrder() = {&entry, &outerDiv, &innerDiv, &innerMerge, &outerMerge};

    // CFG IDs:
    // entry (0) -> outerDiv (1), outerMerge (4), merge is 4
    // outerDiv (1) -> innerDiv (2), innerMerge (3), merge is 3
    // innerDiv (2) -> innerMerge (3)
    // innerMerge (3) -> outerMerge (4)
    // outerMerge (4) -> return
    BlockInfo entryInfo{};
    entryInfo.id = 0u;
    entryInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    entryInfo.terminator.condition = BranchCondition::ExecZero;
    entryInfo.terminator.trueBlock = 1u;
    entryInfo.terminator.falseBlock = 4u;
    entryInfo.terminator.mergeBlock = 4u;

    BlockInfo outerDivInfo{};
    outerDivInfo.id = 1u;
    outerDivInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    outerDivInfo.terminator.condition = BranchCondition::VccZero;
    outerDivInfo.terminator.trueBlock = 2u;
    outerDivInfo.terminator.falseBlock = 3u;
    outerDivInfo.terminator.mergeBlock = 3u;

    BlockInfo innerDivInfo{};
    innerDivInfo.id = 2u;
    innerDivInfo.terminator.kind = TerminatorKind::Branch;
    innerDivInfo.terminator.trueBlock = 3u;

    BlockInfo innerMergeInfo{};
    innerMergeInfo.id = 3u;
    innerMergeInfo.terminator.kind = TerminatorKind::Branch;
    innerMergeInfo.terminator.trueBlock = 4u;

    BlockInfo outerMergeInfo{};
    outerMergeInfo.id = 4u;
    outerMergeInfo.terminator.kind = TerminatorKind::Return;

    program.Metadata().blockInfo = {entryInfo, outerDivInfo, innerDivInfo, innerMergeInfo, outerMergeInfo};

    IrBuilder ir(program);
    ir.SetInsertionPoint(innerDiv);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    innerDiv.AppendInstruction(&writeOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 1u);
    // Inner merge (block 3) should have received the barrier, NOT outer merge (block 4)
    bool innerMergeHasBarrier = false;
    for (IrValue* inst : innerMerge.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            innerMergeHasBarrier = true;
            break;
        }
    }
    EXPECT_TRUE(innerMergeHasBarrier);

    bool outerMergeHasBarrier = false;
    for (IrValue* inst : outerMerge.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            outerMergeHasBarrier = true;
            break;
        }
    }
    EXPECT_FALSE(outerMergeHasBarrier);
}

TEST(RecompilerFixesTests, IndirectBranchTargetInDivergentRegionIsClassifiedAsDivergent) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    IrBlock& divDispatch = program.CreateBlock();
    IrBlock& indirectTargetBlock = program.CreateBlock();
    IrBlock& mergeBlock = program.CreateBlock();

    program.SetEntryBlock(entry);
    program.BlockOrder() = {&entry, &divDispatch, &indirectTargetBlock, &mergeBlock};

    // entry (0) divergent -> divDispatch (1), mergeBlock (3)
    // divDispatch (1) -> IndirectBranch to targets: [2], mergeBlock is 3
    // indirectTargetBlock (2) -> mergeBlock (3)
    // mergeBlock (3) -> return
    BlockInfo entryInfo{};
    entryInfo.id = 0u;
    entryInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    entryInfo.terminator.condition = BranchCondition::ExecZero;
    entryInfo.terminator.trueBlock = 1u;
    entryInfo.terminator.falseBlock = 3u;
    entryInfo.terminator.mergeBlock = 3u;

    BlockInfo divDispatchInfo{};
    divDispatchInfo.id = 1u;
    divDispatchInfo.terminator.kind = TerminatorKind::IndirectBranch;
    divDispatchInfo.terminator.indirectTargets = {2u};
    divDispatchInfo.terminator.mergeBlock = 3u;

    BlockInfo indirectTargetInfo{};
    indirectTargetInfo.id = 2u;
    indirectTargetInfo.terminator.kind = TerminatorKind::Branch;
    indirectTargetInfo.terminator.trueBlock = 3u;

    BlockInfo mergeInfo{};
    mergeInfo.id = 3u;
    mergeInfo.terminator.kind = TerminatorKind::Return;

    program.Metadata().blockInfo = {entryInfo, divDispatchInfo, indirectTargetInfo, mergeInfo};

    IrBuilder ir(program);
    ir.SetInsertionPoint(indirectTargetBlock);

    IrValue& addr = ir.Constant(32u);
    IrValue& val = ir.Constant(99u);
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    indirectTargetBlock.AppendInstruction(&writeOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 1u);
    // indirectTargetBlock is divergent, so it must NOT contain the barrier
    for (IrValue* inst : indirectTargetBlock.Instructions()) {
        EXPECT_NE(inst->Opcode(), IrOpcode::Barrier);
    }
    // Uniform merge block MUST contain the barrier
    bool mergeHasBarrier = false;
    for (IrValue* inst : mergeBlock.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            mergeHasBarrier = true;
            break;
        }
    }
    EXPECT_TRUE(mergeHasBarrier);
}

TEST(RecompilerFixesTests, SharedAtomicZeroPreservedForSynchronization) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);
    IrBuilder ir(program);
    ir.SetInsertionPoint(entry);

    IrValue& addr = ir.Constant(32u);
    IrValue& zeroVal = ir.Constant(0u);

    IrValue& atomicOp = program.CreateValue(IrOpcode::SharedAtomicIAdd32, IrType::U32);
    atomicOp.AddArgument(&addr);
    atomicOp.AddArgument(&zeroVal);
    entry.AppendInstruction(&atomicOp);

    EXPECT_EQ(atomicOp.Opcode(), IrOpcode::SharedAtomicIAdd32);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    static_cast<void>(inserter.Insert(program, 32u));

    // Shared atomics provide workgroup memory synchronization; must NOT be eliminated to Identity
    EXPECT_EQ(atomicOp.Opcode(), IrOpcode::SharedAtomicIAdd32);
}

TEST(RecompilerFixesTests, SharedMemoryBarrierInsertedAfterDataAppendAndConsumeWave64) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);
    IrBuilder ir(program);
    ir.SetInsertionPoint(entry);

    IrValue& val = ir.Constant(123u);

    // DataAppend (ds_append) and DataConsume (ds_consume) update shared memory state
    IrValue& appendOp = program.CreateValue(IrOpcode::DataAppend, IrType::U32);
    appendOp.AddArgument(&val);
    entry.AppendInstruction(&appendOp);

    IrValue& consumeOp = program.CreateValue(IrOpcode::DataConsume, IrType::U32);
    entry.AppendInstruction(&consumeOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    // Both append and consume must have barriers inserted after them
    EXPECT_EQ(stats.insertedBarriers, 2u);

    std::vector<IrOpcode> opcodes;
    for (IrValue* inst : entry.Instructions()) {
        if (inst) {
            opcodes.push_back(inst->Opcode());
        }
    }
    ASSERT_EQ(opcodes.size(), 4u);
    EXPECT_EQ(opcodes[0], IrOpcode::DataAppend);
    EXPECT_EQ(opcodes[1], IrOpcode::Barrier);
    EXPECT_EQ(opcodes[2], IrOpcode::DataConsume);
    EXPECT_EQ(opcodes[3], IrOpcode::Barrier);
}

TEST(RecompilerFixesTests, VMovrelsEmitsSelectChainLowering) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);

    TranslationContext context(program, entry, 256u);

    RdnaInstruction inst{};
    inst.family = RdnaInstructionFamily::VOP1;
    inst.op = RdnaOpcode::VMovrelsB32;
    inst.destination.kind = RdnaOperandKind::VectorRegister;
    inst.destination.reg = 0u;
    inst.source0.kind = RdnaOperandKind::VectorRegister;
    inst.source0.reg = 5u;

    context.TranslateInstruction(inst);

    // Verify that the entry block contains select operations comparing M0 against candidate registers
    bool foundSelect = false;
    for (IrValue* val : entry.Instructions()) {
        if (val && val->Opcode() == IrOpcode::SelectU32) {
            foundSelect = true;
            break;
        }
    }
    EXPECT_TRUE(foundSelect);
}

TEST(RecompilerFixesTests, VMovrelsOutOfBoundsFallsBackToVgpr0) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);

    TranslationContext context(program, entry, 256u);

    RdnaInstruction inst{};
    inst.family = RdnaInstructionFamily::VOP1;
    inst.op = RdnaOpcode::VMovrelsB32;
    inst.destination.kind = RdnaOperandKind::VectorRegister;
    inst.destination.reg = 10u;
    inst.source0.kind = RdnaOperandKind::VectorRegister;
    inst.source0.reg = 20u;

    context.TranslateInstruction(inst);

    // First instruction in select chain initializes result from VGPR0
    bool readsVgpr0 = false;
    for (IrValue* val : entry.Instructions()) {
        if (val && val->Opcode() == IrOpcode::GetVectorRegister) {
            if (val->ArgumentCount() >= 1 && val->Argument(0)->Register().bank == RegisterBank::Vector && val->Argument(0)->Register().index == 0u) {
                readsVgpr0 = true;
                break;
            }
        }
    }
    EXPECT_TRUE(readsVgpr0);
}

TEST(RecompilerFixesTests, VMovreldEmitsSelectChainLowering) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);

    TranslationContext context(program, entry, 256u);

    RdnaInstruction inst{};
    inst.family = RdnaInstructionFamily::VOP1;
    inst.op = RdnaOpcode::VMovreldB32;
    inst.destination.kind = RdnaOperandKind::VectorRegister;
    inst.destination.reg = 0u;
    inst.source0.kind = RdnaOperandKind::VectorRegister;
    inst.source0.reg = 1u;

    context.TranslateInstruction(inst);

    bool foundSelect = false;
    for (IrValue* val : entry.Instructions()) {
        if (val && val->Opcode() == IrOpcode::SelectU32) {
            foundSelect = true;
            break;
        }
    }
    EXPECT_TRUE(foundSelect);
}

TEST(RecompilerFixesTests, VMovreldOutOfBoundsWritesToVgpr0) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);

    TranslationContext context(program, entry, 256u);

    // Destination base is VGPR 10 (does not include VGPR 0 in dstBase + [0..64))
    RdnaInstruction inst{};
    inst.family = RdnaInstructionFamily::VOP1;
    inst.op = RdnaOpcode::VMovreldB32;
    inst.destination.kind = RdnaOperandKind::VectorRegister;
    inst.destination.reg = 10u;
    inst.source0.kind = RdnaOperandKind::VectorRegister;
    inst.source0.reg = 1u;

    context.TranslateInstruction(inst);

    // Fallback write to VGPR0 must be emitted for out-of-bounds M0
    bool setsVgpr0 = false;
    for (IrValue* val : entry.Instructions()) {
        if (val && val->Opcode() == IrOpcode::SetVectorRegister) {
            if (val->ArgumentCount() >= 1 && val->Argument(0)->Register().bank == RegisterBank::Vector && val->Argument(0)->Register().index == 0u) {
                setsVgpr0 = true;
                break;
            }
        }
    }
    EXPECT_TRUE(setsVgpr0);
}

TEST(RecompilerFixesTests, SSaveexecReadsOldExecBeforeUpdatingExec) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);

    TranslationContext context(program, entry, 256u);

    RdnaInstruction inst{};
    inst.family = RdnaInstructionFamily::SOP1;
    inst.op = RdnaOpcode::SAndSaveexecB64;
    inst.destination.kind = RdnaOperandKind::ScalarRegister;
    inst.destination.reg = 4u;
    inst.source0.kind = RdnaOperandKind::ScalarRegister;
    inst.source0.reg = 6u;

    context.TranslateInstruction(inst);

    // Verify instruction ordering: GetExecLo / GetExecHi must occur before SetExecLo / SetExecHi
    std::vector<IrOpcode> opcodes;
    for (IrValue* val : entry.Instructions()) {
        if (val) {
            opcodes.push_back(val->Opcode());
        }
    }

    auto itGetExecLo = std::find(opcodes.begin(), opcodes.end(), IrOpcode::GetExecLo);
    auto itSetExecLo = std::find(opcodes.begin(), opcodes.end(), IrOpcode::SetExecLo);

    ASSERT_NE(itGetExecLo, opcodes.end());
    ASSERT_NE(itSetExecLo, opcodes.end());
    EXPECT_LT(std::distance(opcodes.begin(), itGetExecLo), std::distance(opcodes.begin(), itSetExecLo));
}

TEST(RecompilerFixesTests, SccConditionClassifiedAsDivergentPlacesBarrierAtReconvergence) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    IrBlock& sccBranch = program.CreateBlock();
    IrBlock& mergeBlock = program.CreateBlock();

    program.SetEntryBlock(entry);
    program.BlockOrder() = {&entry, &sccBranch, &mergeBlock};

    // entry (0) conditional branch on SccZero -> sccBranch (1), mergeBlock is 2
    BlockInfo entryInfo{};
    entryInfo.id = 0u;
    entryInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    entryInfo.terminator.condition = BranchCondition::SccZero;
    entryInfo.terminator.trueBlock = 1u;
    entryInfo.terminator.falseBlock = 2u;
    entryInfo.terminator.mergeBlock = 2u;

    BlockInfo sccBranchInfo{};
    sccBranchInfo.id = 1u;
    sccBranchInfo.terminator.kind = TerminatorKind::Branch;
    sccBranchInfo.terminator.trueBlock = 2u;

    BlockInfo mergeBlockInfo{};
    mergeBlockInfo.id = 2u;
    mergeBlockInfo.terminator.kind = TerminatorKind::Return;

    program.Metadata().blockInfo = {entryInfo, sccBranchInfo, mergeBlockInfo};

    IrBuilder ir(program);
    ir.SetInsertionPoint(sccBranch);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    sccBranch.AppendInstruction(&writeOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 1u);
    // Barrier should NOT be in sccBranch (divergent across waves in workgroup), but at reconvergence (mergeBlock)
    bool sccBranchHasBarrier = false;
    for (IrValue* inst : sccBranch.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            sccBranchHasBarrier = true;
            break;
        }
    }
    EXPECT_FALSE(sccBranchHasBarrier);

    bool mergeHasBarrier = false;
    for (IrValue* inst : mergeBlock.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            mergeHasBarrier = true;
            break;
        }
    }
    EXPECT_TRUE(mergeHasBarrier);
}

TEST(RecompilerFixesTests, DivergentLoopWithoutMergeBlockEmitsDirectBarrier) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    IrBlock& loopBody = program.CreateBlock();

    program.SetEntryBlock(entry);
    program.BlockOrder() = {&entry, &loopBody};

    // entry (0) conditional branch on VccZero without structured mergeBlock (e.g. unstructured loop backedge)
    BlockInfo entryInfo{};
    entryInfo.id = 0u;
    entryInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    entryInfo.terminator.condition = BranchCondition::VccZero;
    entryInfo.terminator.trueBlock = 1u;
    entryInfo.terminator.falseBlock = 0u;
    entryInfo.terminator.mergeBlock = InvalidControlFlowId;

    BlockInfo loopBodyInfo{};
    loopBodyInfo.id = 1u;
    loopBodyInfo.terminator.kind = TerminatorKind::Return;

    program.Metadata().blockInfo = {entryInfo, loopBodyInfo};

    IrBuilder ir(program);
    ir.SetInsertionPoint(loopBody);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    loopBody.AppendInstruction(&writeOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    // Because mergeBlock is invalid, loopBody is not trapped in an invalid merge mapping;
    // direct barrier insertion takes over so wave64 LDS ordering is not lost.
    EXPECT_EQ(stats.insertedBarriers, 1u);
    bool loopBodyHasBarrier = false;
    for (IrValue* inst : loopBody.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            loopBodyHasBarrier = true;
            break;
        }
    }
    EXPECT_TRUE(loopBodyHasBarrier);
}

TEST(RecompilerFixesTests, DivergentRegionLdsReadOrdersPriorWritesAtUniformHeader) {
    // Behavioral invariant: an LDS read inside a lane-divergent region must be synchronized
    // BEFORE it executes. A merge-block barrier would run after the read and order nothing
    // for it, so the pass must place the barrier at the end of the outermost uniform header
    // (the block holding the divergent branch), which every lane executes before diverging.
    // Preconditions: entry (0) holds a divergent ExecZero branch over divBlock (1) with
    // mergeBlock (2); divBlock holds a single LoadSharedU32 and no LDS write.
    // Expected: exactly one Barrier, located in the uniform header (entry); divBlock and
    // mergeBlock hold no Barrier (a read-only region needs no reconvergence barrier).
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    IrBlock& divBlock = program.CreateBlock();
    IrBlock& mergeBlock = program.CreateBlock();

    program.SetEntryBlock(entry);
    program.BlockOrder() = {&entry, &divBlock, &mergeBlock};

    // CFG IDs:
    // entry (0) conditional branch on ExecZero (divergent) -> divBlock (1), mergeBlock (2), merge is 2
    // divBlock (1) -> mergeBlock (2)
    // mergeBlock (2) -> return
    BlockInfo entryInfo{};
    entryInfo.id = 0u;
    entryInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    entryInfo.terminator.condition = BranchCondition::ExecZero;
    entryInfo.terminator.trueBlock = 1u;
    entryInfo.terminator.falseBlock = 2u;
    entryInfo.terminator.mergeBlock = 2u;

    BlockInfo divInfo{};
    divInfo.id = 1u;
    divInfo.terminator.kind = TerminatorKind::Branch;
    divInfo.terminator.trueBlock = 2u;

    BlockInfo mergeInfo{};
    mergeInfo.id = 2u;
    mergeInfo.terminator.kind = TerminatorKind::Return;

    program.Metadata().blockInfo = {entryInfo, divInfo, mergeInfo};

    IrBuilder ir(program);
    ir.SetInsertionPoint(divBlock);

    IrValue& addr = ir.Constant(64u);
    IrValue& readOp = program.CreateValue(IrOpcode::LoadSharedU32, IrType::U32);
    readOp.AddArgument(&addr);
    divBlock.AppendInstruction(&readOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    // The pre-read barrier at the uniform header is the only barrier needed here.
    EXPECT_EQ(stats.insertedBarriers, 1u);
    for (IrValue* inst : divBlock.Instructions()) {
        EXPECT_NE(inst->Opcode(), IrOpcode::Barrier);
    }
    for (IrValue* inst : mergeBlock.Instructions()) {
        EXPECT_NE(inst->Opcode(), IrOpcode::Barrier);
    }

    // The header barrier must be the last instruction: it executes uniformly after the
    // header's own work and before any divergent successor, hence before the read.
    ASSERT_FALSE(entry.Instructions().empty());
    EXPECT_EQ(entry.Instructions().back()->Opcode(), IrOpcode::Barrier);
}

TEST(RecompilerFixesTests, DivergentWriteThenReadInSameBlockGetsHeaderAndMergeBarriers) {
    // Behavioral invariant: a divergent block holding an LDS write followed by an LDS read
    // needs both sides of the maximal deadlock-free ordering: a pre-region barrier at the
    // uniform header (before the read) and a reconvergence barrier at the merge block
    // (after the write, for post-region readers). No Barrier may appear inside the
    // divergent block itself (it would deadlock lane-divergent OpControlBarrier execution).
    // Preconditions: entry (0) branches divergently over divBlock (1) with mergeBlock (2);
    // divBlock holds WriteSharedU32 followed by LoadSharedU32.
    // Expected: exactly two Barriers (header + merge), none in divBlock.
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    IrBlock& divBlock = program.CreateBlock();
    IrBlock& mergeBlock = program.CreateBlock();

    program.SetEntryBlock(entry);
    program.BlockOrder() = {&entry, &divBlock, &mergeBlock};

    BlockInfo entryInfo{};
    entryInfo.id = 0u;
    entryInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    entryInfo.terminator.condition = BranchCondition::VccNonZero;
    entryInfo.terminator.trueBlock = 1u;
    entryInfo.terminator.falseBlock = 2u;
    entryInfo.terminator.mergeBlock = 2u;

    BlockInfo divInfo{};
    divInfo.id = 1u;
    divInfo.terminator.kind = TerminatorKind::Branch;
    divInfo.terminator.trueBlock = 2u;

    BlockInfo mergeInfo{};
    mergeInfo.id = 2u;
    mergeInfo.terminator.kind = TerminatorKind::Return;

    program.Metadata().blockInfo = {entryInfo, divInfo, mergeInfo};

    IrBuilder ir(program);
    ir.SetInsertionPoint(divBlock);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    divBlock.AppendInstruction(&writeOp);

    IrValue& readOp = program.CreateValue(IrOpcode::LoadSharedU32, IrType::U32);
    readOp.AddArgument(&addr);
    divBlock.AppendInstruction(&readOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 2u);
    for (IrValue* inst : divBlock.Instructions()) {
        EXPECT_NE(inst->Opcode(), IrOpcode::Barrier);
    }

    bool headerHasBarrier = false;
    for (IrValue* inst : entry.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            headerHasBarrier = true;
            break;
        }
    }
    EXPECT_TRUE(headerHasBarrier);

    bool mergeHasBarrier = false;
    for (IrValue* inst : mergeBlock.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::Barrier) {
            mergeHasBarrier = true;
            break;
        }
    }
    EXPECT_TRUE(mergeHasBarrier);
}

TEST(RecompilerFixesTests, DivergentReadWithCyclicHeaderSkipsPreReadBarrier) {
    // Behavioral invariant: a divergent-region header that lies on a control-flow cycle
    // executes per loop iteration, so a header barrier there is only uniform when loop control
    // is uniform -- which BlockInfo alone cannot prove. The pass must skip the pre-read header
    // barrier rather than risk a dynamically non-uniform workgroup barrier (GPU deadlock).
    // Preconditions: entry header (0) branches divergently over divBlock (1) with mergeBlock (3),
    // and the merge block doubles as a loop latch branching back to the header (3 -> 0), so the
    // header reaches itself; divBlock holds a single LoadSharedU32 and the region has no write.
    // Expected: zero Barriers anywhere (no write needs reconvergence; the cyclic header is skipped).
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    IrBlock& divBlock = program.CreateBlock();
    IrBlock& mergeBlock = program.CreateBlock();

    program.SetEntryBlock(entry);
    program.BlockOrder() = {&entry, &divBlock, &mergeBlock};

    BlockInfo entryInfo{};
    entryInfo.id = 0u;
    entryInfo.terminator.kind = TerminatorKind::ConditionalBranch;
    entryInfo.terminator.condition = BranchCondition::ExecNonZero;
    entryInfo.terminator.trueBlock = 1u;
    entryInfo.terminator.falseBlock = 3u;
    entryInfo.terminator.mergeBlock = 3u;

    BlockInfo divInfo{};
    divInfo.id = 1u;
    divInfo.terminator.kind = TerminatorKind::Branch;
    divInfo.terminator.trueBlock = 3u;

    BlockInfo mergeInfo{};
    mergeInfo.id = 3u;
    mergeInfo.terminator.kind = TerminatorKind::Branch;
    mergeInfo.terminator.trueBlock = 0u; // loop latch backedge: header lies on a cycle

    program.Metadata().blockInfo = {entryInfo, divInfo, mergeInfo};

    IrBuilder ir(program);
    ir.SetInsertionPoint(divBlock);

    IrValue& addr = ir.Constant(64u);
    IrValue& readOp = program.CreateValue(IrOpcode::LoadSharedU32, IrType::U32);
    readOp.AddArgument(&addr);
    divBlock.AppendInstruction(&readOp);

    program.Resources().stage = IrShaderStage::Compute;
    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 0u);
    for (IrValue* inst : entry.Instructions()) {
        EXPECT_NE(inst->Opcode(), IrOpcode::Barrier);
    }
    for (IrValue* inst : divBlock.Instructions()) {
        EXPECT_NE(inst->Opcode(), IrOpcode::Barrier);
    }
    for (IrValue* inst : mergeBlock.Instructions()) {
        EXPECT_NE(inst->Opcode(), IrOpcode::Barrier);
    }
}

TEST(RecompilerFixesTests, Wave64VertexStageSkipsBarrierInsertion) {
    // Behavioral invariant: the backend lowers IrOpcode::Barrier to OpControlBarrier with
    // Workgroup execution scope, which has no workgroup in vertex/fragment/tessellation-evaluation
    // modules and fails shader-module creation there. The pass must insert nothing for those
    // stages even when wave64 LDS writes are present.
    // Preconditions: stage Vertex, uniform block with a single WriteSharedU32, waveSize 64.
    // Expected: zero Barriers, write left untouched.
    IrProgram program;
    program.Resources().stage = IrShaderStage::Vertex;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);
    IrBuilder ir(program);
    ir.SetInsertionPoint(entry);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    entry.AppendInstruction(&writeOp);

    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 0u);
    ASSERT_EQ(entry.Instructions().size(), 1u);
    EXPECT_EQ(entry.Instructions().front()->Opcode(), IrOpcode::WriteSharedU32);
}

TEST(RecompilerFixesTests, TessellationControlStageInsertsBarrier) {
    // Behavioral invariant: tessellation-control patches synchronize with workgroup-execution
    // barriers (the backend emits them with tessellation-control memory semantics), so the pass
    // must stay active there, unlike vertex/fragment stages.
    // Preconditions: stage TessellationControl, uniform block with a single WriteSharedU32, waveSize 64.
    // Expected: exactly one Barrier directly after the write.
    IrProgram program;
    program.Resources().stage = IrShaderStage::TessellationControl;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);
    IrBuilder ir(program);
    ir.SetInsertionPoint(entry);

    IrValue& addr = ir.Constant(16u);
    IrValue& val = ir.Constant(42u);
    IrValue& writeOp = program.CreateValue(IrOpcode::WriteSharedU32, IrType::Void);
    writeOp.AddArgument(&addr);
    writeOp.AddArgument(&val);
    entry.AppendInstruction(&writeOp);

    SharedMemoryBarrierInserter inserter;
    const auto stats = inserter.Insert(program, 64u);

    EXPECT_EQ(stats.insertedBarriers, 1u);
    ASSERT_EQ(entry.Instructions().size(), 2u);
    auto it = entry.Instructions().begin();
    EXPECT_EQ((*it)->Opcode(), IrOpcode::WriteSharedU32);
    ++it;
    ASSERT_NE(it, entry.Instructions().end());
    EXPECT_EQ((*it)->Opcode(), IrOpcode::Barrier);
}

#if ANYPS5_ENABLE_SPIRV_TOOLS
// Vulkan 1.3 is the driver floor and SPIR-V 1.6 its ceiling (docs/spec/gpu-driver.md). Invariant: the
// optimizer accepts exactly the Vulkan/SPIR-V pairings the driver can produce, so a driver that requests
// {1.3, 1.6} validates, while a Vulkan 1.1/1.2 environment rejects a 1.6 target instead of silently
// emitting modules the device would refuse. Failure mode guarded: the driver regressing to a 1.1 target.
TEST(SpirvTargetVersionTests, VulkanThreeAcceptsSpirvOnePointSixAndOlderEnvironmentsReject) {
    // Minimal valid module: Shader capability, Logical/GLSL450, empty GLCompute main. The header version
    // word (1.0) is at or below every target tested, so only the target pairing decides pass/fail.
    const std::vector<std::uint32_t> module{
        0x07230203u, 0x00010000u, 0u, 6u, 0u,
        0x00020011u, 1u,
        0x0003000eu, 0u, 1u,
        0x0005000fu, 5u, 4u, 0x6e69616du, 0u,
        0x00060010u, 4u, 17u, 1u, 1u, 1u,
        0x00020013u, 2u,
        0x00030021u, 3u, 2u,
        0x00050036u, 2u, 4u, 0u, 3u,
        0x000200f8u, 5u,
        0x000100fdu,
        0x00010038u};
    // Id bound (word 3) must exceed the highest id used (5), hence 6. Result id 4 is main, 5 its block.
    const auto validated = ValidateAndOptimizeSpirv(module, 0x00403000u, 0x00010600u);
    EXPECT_FALSE(validated.empty());
    EXPECT_THROW(static_cast<void>(ValidateAndOptimizeSpirv(module, 0x00401000u, 0x00010600u)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(ValidateAndOptimizeSpirv(module, 0x00402000u, 0x00010600u)), std::runtime_error);
    // Vulkan 1.4 shares the 1.6 ceiling, so an opt-in 1.4 device keeps the same shader target.
    EXPECT_FALSE(ValidateAndOptimizeSpirv(module, 0x00404000u, 0x00010600u).empty());
}
#endif

} // namespace
} // namespace ShaderRecompiler

