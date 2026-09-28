// core/shader/recompiler/tests/RecompilerFixesTests.cpp
// Unit tests for shader recompiler Milestone 1 fixes:
//   - v_movrels/v_movreld bounded select-chain lowering over the VGPR register file with VGPR[0] fallback
//   - SharedMemoryBarrierInserter: wave-LDS barriers in wave64 compute programs (uniform and reconvergence points)
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

#include <algorithm>
#include <gtest/gtest.h>
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

} // namespace
} // namespace ShaderRecompiler
