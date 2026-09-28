// core/shader/recompiler/tests/RecompilerFixesTests.cpp
// Unit tests for shader recompiler Milestone 1 fixes:
//   - v_movrels/v_movreld bounded select-chain lowering over the VGPR register file
//   - SharedMemoryBarrierInserter: wave-LDS barriers in wave64 compute programs
//   - Atomic-zero peephole: eliminate or skip SharedAtomicIAdd32 with immediate zero addend
//
// These tests verify compiler IR transformations and invariants using GoogleTest.

#include "Optimization/include/Optimization/SharedMemoryBarrierInserter.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrBlock.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrBuilder.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrOpcode.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrProgram.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrValue.hpp"

#include <gtest/gtest.h>

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

} // namespace
} // namespace ShaderRecompiler
