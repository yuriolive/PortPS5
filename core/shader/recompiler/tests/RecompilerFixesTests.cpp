// core/shader/recompiler/tests/RecompilerFixesTests.cpp
// Unit tests for shader recompiler Milestone 1 fixes:
//   - v_movrels/v_movreld bounded select-chain lowering over the VGPR register file
//   - SharedMemoryBarrierInserter: wave-LDS barriers in wave64 compute programs
//   - Atomic-zero peephole: eliminate or skip SharedAtomicIAdd32 with immediate zero addend
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

TEST(RecompilerFixesTests, AtomicZeroEliminatedWhenResultUnused) {
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

    // Since atomicOp result has no uses, it should be changed to Identity
    EXPECT_EQ(atomicOp.Opcode(), IrOpcode::Identity);
}

TEST(RecompilerFixesTests, NonZeroAtomicNotEliminated) {
    IrProgram program;
    IrBlock& entry = program.CreateBlock();
    program.SetEntryBlock(entry);
    IrBuilder ir(program);
    ir.SetInsertionPoint(entry);

    IrValue& addr = ir.Constant(32u);
    IrValue& nonZero = ir.Constant(1u);

    IrValue& atomicOp = program.CreateValue(IrOpcode::SharedAtomicIAdd32, IrType::U32);
    atomicOp.AddArgument(&addr);
    atomicOp.AddArgument(&nonZero);
    entry.AppendInstruction(&atomicOp);

    SharedMemoryBarrierInserter inserter;
    static_cast<void>(inserter.Insert(program, 32u));

    EXPECT_EQ(atomicOp.Opcode(), IrOpcode::SharedAtomicIAdd32);
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
