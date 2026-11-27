//===- FilCCBatchReads.cpp - Checked read batching -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FilCCBatchReads.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <limits>

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {

constexpr unsigned BatchWidth = 4;

bool isLaneInstruction(const Instruction &I) {
  if (!I.getType()->isIntegerTy())
    return false;
  switch (I.getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
    return true;
  default:
    return false;
  }
}

void batchLoop(BasicBlock &Loop) {
  auto *Latch = dyn_cast<BranchInst>(Loop.getTerminator());
  if (!Latch || !Latch->isConditional())
    return;
  auto *Test = dyn_cast<ICmpInst>(Latch->getCondition());
  if (!Test || Test->getParent() != &Loop || !Test->hasOneUse() ||
      !Test->isEquality() ||
      Latch->getSuccessor(Test->getPredicate() == ICmpInst::ICMP_EQ) != &Loop ||
      Latch->getSuccessor(Test->getPredicate() != ICmpInst::ICMP_EQ) == &Loop)
    return;

  BasicBlock *Entry = nullptr;
  for (BasicBlock *Pred : predecessors(&Loop)) {
    if (Pred == &Loop)
      continue;
    if (Entry)
      return;
    Entry = Pred;
  }
  if (!Entry || !isa<BranchInst>(Entry->getTerminator()))
    return;

  LoadInst *Read = nullptr;
  GetElementPtrInst *Address = nullptr;
  for (Instruction &I : Loop) {
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (Read || !LI->isSimple())
        return;
      Read = LI;
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
      if (Address)
        return;
      Address = GEP;
    } else if (!isa<PHINode>(I) && &I != Test && &I != Latch &&
               !isLaneInstruction(I)) {
      return;
    }
  }

  if (!Read || !Address || Read->getPointerOperand() != Address ||
      !Address->hasOneUse() || Address->getNumIndices() != 1 ||
      Address->getPointerAddressSpace() != 0 ||
      !isa<Argument>(Address->getPointerOperand()) ||
      Read->getType() != Address->getSourceElementType())
    return;
  Type *Element = Read->getType();
  if (!Element->isIntegerTy(8) && !Element->isIntegerTy(16) &&
      !Element->isIntegerTy(32) && !Element->isIntegerTy(64))
    return;

  auto *Index = dyn_cast<PHINode>(Address->getOperand(1));
  if (!Index || Index->getParent() != &Loop ||
      !Index->getType()->isIntegerTy(64) ||
      !match(Index->getIncomingValueForBlock(Entry), m_Zero()))
    return;
  Value *Next = Index->getIncomingValueForBlock(&Loop);
  if (!match(Next, m_c_Add(m_Specific(Index), m_One())))
    return;
  if (Test->getOperand(0) != Next && Test->getOperand(1) != Next)
    return;
  Value *Count = Test->getOperand(Test->getOperand(0) == Next ? 1 : 0);
  if (!isa<Argument, ConstantInt>(Count))
    return;

  // Keep one to four scalar iterations. All original exit values then come
  // from the original loop, even when the count is a multiple of BatchWidth.
  Function *F = Loop.getParent();
  LLVMContext &C = Loop.getContext();
  BasicBlock *Guard = BasicBlock::Create(C, "filcc.batch.guard", F, &Loop);
  BasicBlock *Batch = BasicBlock::Create(C, "filcc.batch", F, &Loop);
  BasicBlock *Tail = BasicBlock::Create(C, "filcc.batch.tail", F, &Loop);
  IRBuilder<> B(Guard);
  Value *StableCount = B.CreateFreeze(Count);
  Value *Enough = B.CreateICmpUGT(StableCount, B.getInt64(BatchWidth));
  // Every index in the batch is representable as a positive signed byte
  // offset. The checked vector load still validates the resulting address.
  uint64_t MaxCount = std::numeric_limits<int64_t>::max() /
                      (Element->getIntegerBitWidth() / 8);
  Value *Fits = B.CreateICmpULE(StableCount, B.getInt64(MaxCount));
  B.CreateCondBr(B.CreateAnd(Enough, Fits), Batch, Tail);
  Entry->getTerminator()->replaceUsesOfWith(&Loop, Guard);

  B.SetInsertPoint(Batch);
  ValueToValueMapTy Map;
  SmallVector<PHINode *, 4> BatchPhis;
  for (PHINode &Phi : Loop.phis()) {
    auto *BatchPhi = B.CreatePHI(Phi.getType(), 2, "filcc.batch.state");
    BatchPhi->addIncoming(Phi.getIncomingValueForBlock(Entry), Guard);
    BatchPhis.push_back(BatchPhi);
    Map[&Phi] = BatchPhi;
  }

  B.SetCurrentDebugLocation(Read->getDebugLoc());
  Value *Start = B.CreateGEP(Element, Address->getPointerOperand(), Map[Index]);
  auto *Vector = FixedVectorType::get(Element, BatchWidth);
  // A normal Fil-C memory operation: lowering checks all vector bytes.
  auto *Wide = B.CreateAlignedLoad(Vector, Start, Align(1), "filcc.batch.read");
  SmallVector<Value *, 4> Updates(BatchPhis.size());
  for (unsigned Lane = 0; Lane != BatchWidth; ++Lane) {
    Map[Read] = B.CreateExtractElement(Wide, B.getInt32(Lane));
    for (Instruction &I : Loop) {
      if (isa<PHINode>(I) || &I == Address || &I == Read || &I == Test ||
          &I == Latch)
        continue;
      Instruction *Copy = I.clone();
      Copy->dropPoisonGeneratingAnnotations();
      Copy->insertInto(Batch, Batch->end());
      RemapInstruction(Copy, Map, RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      Map[&I] = Copy;
    }
    unsigned P = 0;
    for (PHINode &Phi : Loop.phis()) {
      Value *Update = Phi.getIncomingValueForBlock(&Loop);
      auto It = Map.find(Update);
      Updates[P++] = It != Map.end() ? static_cast<Value *>(It->second) : Update;
    }
    P = 0;
    for (PHINode &Phi : Loop.phis())
      Map[&Phi] = Updates[P++];
  }

  Value *Remaining = B.CreateSub(StableCount, Map[Index]);
  B.CreateCondBr(B.CreateICmpUGT(Remaining, B.getInt64(BatchWidth)), Batch, Tail);
  B.SetInsertPoint(Tail);
  unsigned P = 0;
  for (PHINode &Phi : Loop.phis()) {
    auto *TailPhi = B.CreatePHI(Phi.getType(), 2, "filcc.tail.state");
    TailPhi->addIncoming(Phi.getIncomingValueForBlock(Entry), Guard);
    TailPhi->addIncoming(Updates[P], Batch);
    BatchPhis[P]->addIncoming(Updates[P], Batch);
    ++P;
    Phi.setIncomingValueForBlock(Entry, TailPhi);
    Phi.setIncomingBlock(Phi.getBasicBlockIndex(Entry), Tail);
  }
  B.CreateBr(&Loop);
}

} // namespace

void llvm::filccBatchReads(Function &F) {
  if (F.hasOptNone() || F.hasOptSize())
    return;
  SmallVector<BasicBlock *, 8> Blocks;
  for (BasicBlock &BB : F)
    Blocks.push_back(&BB);
  for (BasicBlock *BB : Blocks)
    batchLoop(*BB);
}
