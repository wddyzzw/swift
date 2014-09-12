//===--- SILValue.cpp - Implementation for SILValue -----------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/SIL/SILValue.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILBasicBlock.h"

using namespace swift;

void SILValue::replaceAllUsesWith(SILValue V) {
  assert(*this != V && "Cannot RAUW a value with itself");
  assert(getType() == V.getType() && "Invalid type");
  while (!use_empty())
    (**use_begin()).set(V);
}

static bool isRCIdentityPreservingCast(ValueKind Kind) {
  switch (Kind) {
  case ValueKind::UpcastInst:
  case ValueKind::AddressToPointerInst:
  case ValueKind::PointerToAddressInst:
  case ValueKind::UncheckedRefCastInst:
  case ValueKind::UncheckedAddrCastInst:
  case ValueKind::RefToRawPointerInst:
  case ValueKind::RawPointerToRefInst:
  case ValueKind::UnconditionalCheckedCastInst:
  case ValueKind::UncheckedRefBitCastInst:
    return true;
  default:
    return false;
  }
}

/// Return the underlying SILValue after stripping off identity SILArguments if
/// we belong to a BB with one predecessor.
static SILValue stripSinglePredecessorArgs(SILValue V) {
  while (true) {
    auto *A = dyn_cast<SILArgument>(V);
    if (!A)
      return V;

    SILBasicBlock *BB = A->getParent();

    // First try and grab the single predecessor of our parent BB. If we don't
    // have one, bail.
    SILBasicBlock *Pred = BB->getSinglePredecessor();
    if (!Pred)
      return V;

    // Then grab the terminator of Pred...
    TermInst *PredTI = Pred->getTerminator();

    // And attempt to find our matching argument.
    if (auto *BI = dyn_cast<BranchInst>(PredTI)) {
      V = BI->getArg(A->getIndex());
      continue;
    }

    if (auto *CBI = dyn_cast<CondBranchInst>(PredTI)) {
      if (SILValue Arg = CBI->getArgForDestBB(BB, A)) {
        V = Arg;
        continue;
      }
    }

    return V;
  }
}

SILValue SILValue::stripCasts() {
  SILValue V = *this;

  while (true) {
    V = stripSinglePredecessorArgs(V);

    auto K = V->getKind();
    if (isRCIdentityPreservingCast(K) ||
        K == ValueKind::UncheckedTrivialBitCastInst) {
      V = cast<SILInstruction>(V.getDef())->getOperand(0);
      continue;
    }

    return V;
  }
}

SILValue SILValue::stripAddressProjections() {
  SILValue V = *this;

  while (true) {
    V = stripSinglePredecessorArgs(V);

    switch (V->getKind()) {
    case ValueKind::StructElementAddrInst:
    case ValueKind::TupleElementAddrInst:
    case ValueKind::RefElementAddrInst:
      V = cast<SILInstruction>(V.getDef())->getOperand(0);
      continue;
    default:
      return V;
    }
  }
}

SILValue SILValue::stripAggregateProjections() {
  SILValue V = *this;

  while (true) {
    V = stripSinglePredecessorArgs(V);

    switch (V->getKind()) {
    case ValueKind::StructExtractInst:
    case ValueKind::TupleExtractInst:
      V = cast<SILInstruction>(V.getDef())->getOperand(0);
      continue;
    default:
      return V;
    }
  }
}

SILValue SILValue::stripIndexingInsts() {
  SILValue V = *this;
  while (true) {
    if (!isa<IndexingInst>(V.getDef()))
      return V;
    V = cast<IndexingInst>(V)->getBase();
  }
}

SILBasicBlock *ValueBase::getParentBB() {
  if (auto Inst = dyn_cast<SILInstruction>(this))
    return Inst->getParent();
  if (auto Arg = dyn_cast<SILArgument>(this))
    return Arg->getParent();
  return nullptr;
}
