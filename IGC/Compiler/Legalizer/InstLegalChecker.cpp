/*===================== begin_copyright_notice ==================================

Copyright (c) 2017 Intel Corporation

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


======================= end_copyright_notice ==================================*/

// vim:ts=2:sw=2:et:

#define DEBUG_TYPE "type-legalizer"
#include "TypeLegalizer.h"
#include "InstLegalChecker.h"
#include "common/LLVMWarningsPush.hpp"

#include "llvmWrapper/Support/Debug.h"

#include "llvm/Support/raw_ostream.h"
#include "common/LLVMWarningsPop.hpp"
using namespace llvm;
using namespace IGC::Legalizer;

// By default, capture all missing instructions!
LegalizeAction InstLegalChecker::visitInstruction(Instruction& I) {
    LLVM_DEBUG(dbgs() << "LEGAL-CHECK: " << I << '\n');
    llvm_unreachable("UNKNOWN INSTRUCTION IS BEING LEGAL-CHECKED!");
}

/// Terminator instructions
///

LegalizeAction InstLegalChecker::visitReturnInst(ReturnInst& I) {
    // It's legal iff the return value (if any) is legal.
    if (Value * V = I.getReturnValue())
        return TL->getTypeLegalizeAction(V->getType());
    // Otherwise, 'ret void' is always legal.
    return Legal;
}

LegalizeAction InstLegalChecker::visitTerminatorInst(IGCLLVM::TerminatorInst&) {
    // FIXME: Shall we treat all terminator insts as legal, e.g. do we
    // support 'indirectbr' or 'resume'.
    return Legal;
}

/// Standard binary operators
///

LegalizeAction InstLegalChecker::visitBinaryOperator(BinaryOperator& I) {
    // It's legal iff the return type is legal (as two operands have the same
    // type as the result value, even for shift operators.)
    return TL->getTypeLegalizeAction(I.getType());
}

/// Memory operators
///

LegalizeAction InstLegalChecker::visitAllocaInst(AllocaInst& I) {
    LegalizeAction Act;
    // FIXME: Do we really check the allocated type?
    // It's legal if the allocated type is legal.
    if ((Act = TL->getTypeLegalizeAction(I.getAllocatedType())) != Legal)
        return Act;
    // If it's array allocation, check array size as well.
    if (I.isArrayAllocation())
        return TL->getTypeLegalizeAction(I.getArraySize()->getType());
    return Legal;
}

LegalizeAction InstLegalChecker::visitLoadInst(LoadInst& I) {
    // It's legal iff the result is legal.
    return TL->getTypeLegalizeAction(I.getType());
}

LegalizeAction InstLegalChecker::visitStoreInst(StoreInst& I) {
    // It's legal iff the value operand is legal.
    return TL->getTypeLegalizeAction(I.getValueOperand()->getType());
}

LegalizeAction InstLegalChecker::visitGetElementPtrInst(GetElementPtrInst& I) {
    LegalizeAction Act;
    // If the result type is illegal, i.e. vector of pointers, it's illegal.
    if ((Act = TL->getTypeLegalizeAction(I.getType())) != Legal)
        return Act;
    // Otherwise, check all index operands.
    for (auto II = I.idx_begin(), IE = I.idx_end(); II != IE; ++II)
        if ((Act = TL->getTypeLegalizeAction((*II)->getType())) != Legal)
            return Act;
    return Legal;
}

LegalizeAction InstLegalChecker::visitFenceInst(FenceInst&) {
    // FIXME: Do we have illegal cases?
    return Legal;
}

LegalizeAction InstLegalChecker::visitAtomicCmpXchgInst(AtomicCmpXchgInst&) {
    // FIXME: Do we have illegal cases?
    return Legal;
}

LegalizeAction InstLegalChecker::visitAtomicRMWInst(AtomicRMWInst&) {
    // FIXME: Do we have illegal cases?
    return Legal;
}

/// Cast operators
///

LegalizeAction InstLegalChecker::visitCastInst(CastInst& I) {
    LegalizeAction Act;
    // It's legal iff both the result and source are legal.
    if ((Act = TL->getTypeLegalizeAction(I.getDestTy())) != Legal)
        return Act;
    return TL->getTypeLegalizeAction(I.getSrcTy());
}

/// Other operators
///

LegalizeAction InstLegalChecker::visitCmpInst(CmpInst& I) {
    LegalizeAction Act;
    // It's legal iff the return value and one of its operands (both operands
    // has the same type) are legal.
    if ((Act = TL->getTypeLegalizeAction(I.getType())) != Legal)
        return Act;
    return TL->getTypeLegalizeAction(I.getOperand(0)->getType());
}

LegalizeAction InstLegalChecker::visitPHINode(PHINode& I) {
    // It's legal iff the result is legal (as all value operands have the
    // same type.
    return TL->getTypeLegalizeAction(I.getType());
}

LegalizeAction InstLegalChecker::visitIntrinsicInst(IntrinsicInst& I) {
    switch (I.getIntrinsicID()) {
        // Intrinsics on floating point are legal iff their result types are
        // legal.
    case Intrinsic::fma:
    case Intrinsic::fmuladd:
    case Intrinsic::sqrt:
    case Intrinsic::powi:
    case Intrinsic::sin:
    case Intrinsic::cos:
    case Intrinsic::pow:
    case Intrinsic::log:
    case Intrinsic::log10:
    case Intrinsic::log2:
    case Intrinsic::exp:
    case Intrinsic::exp2:
    case Intrinsic::fabs:
    case Intrinsic::copysign:
    case Intrinsic::floor:
    case Intrinsic::ceil:
    case Intrinsic::trunc:
    case Intrinsic::rint:
    case Intrinsic::nearbyint:
        // case Intrinsic::round:
        return TL->getTypeLegalizeAction(I.getType());
        // Intrinsics on integer are legal iff their result types are legal.
    case Intrinsic::bswap:
    case Intrinsic::ctpop:
    case Intrinsic::ctlz:
    case Intrinsic::cttz:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::ssub_with_overflow:
    case Intrinsic::usub_with_overflow:
    case Intrinsic::smul_with_overflow:
    case Intrinsic::umul_with_overflow:
        return TL->getTypeLegalizeAction(I.getType());
    default:
        // By default, all intrinsics are regarded as being legal.
        break;
    }
    return Legal;
}

LegalizeAction InstLegalChecker::visitGenIntrinsicInst(GenIntrinsicInst& I) {
    // By default, all Gen intrinsics are regarded as being legal.
    return Legal;
}

LegalizeAction InstLegalChecker::visitCallInst(CallInst& I) {
    // Check Gen intrinsic instruction separately.
    if (isa<GenIntrinsicInst>(&I))
        return visitGenIntrinsicInst(static_cast<GenIntrinsicInst&>(I));
    // FIXME: So far, calls (including GenISA intrinsics) are treated as
    // being legal.
    return Legal;
}

LegalizeAction InstLegalChecker::visitSelectInst(SelectInst& I) {
    LegalizeAction Act;
    // It's legal iff the result and the condition operand are legal (as two
    // value operands has the same type).
    if ((Act = TL->getTypeLegalizeAction(I.getType())) != Legal)
        return Act;
    return TL->getTypeLegalizeAction(I.getCondition()->getType());
}

LegalizeAction InstLegalChecker::visitVAArgInst(VAArgInst&) {
    // FIXME: Do we support it?
    return Legal;
}

LegalizeAction
InstLegalChecker::visitExtractElementInst(ExtractElementInst& I) {
    LegalizeAction Act;
    // It's legal iff the result and all operands are legal. Check return value
    // first.
    if ((Act = TL->getTypeLegalizeAction(I.getType())) != Legal)
        return Act;
    // Check vector operand.
    if ((Act =
        TL->getTypeLegalizeAction(I.getVectorOperand()->getType())) != Legal)
        return Act;
    // Check index operand.
    return TL->getTypeLegalizeAction(I.getIndexOperand()->getType());
}

LegalizeAction InstLegalChecker::visitInsertElementInst(InsertElementInst& I) {
    LegalizeAction Act;
    // It's legal iff the result and all operands are legal. Check return value
    // first. No need to check vector operand which has the same type of return
    // value.
    if ((Act = TL->getTypeLegalizeAction(I.getType())) != Legal)
        return Act;
    // Check index operand
    if ((Act = TL->getTypeLegalizeAction(I.getOperand(1)->getType())) != Legal)
        return Act;
    // Check scalar operand.
    return TL->getTypeLegalizeAction(I.getOperand(2)->getType());
}

LegalizeAction InstLegalChecker::visitShuffleVectorInst(ShuffleVectorInst& I) {
    LegalizeAction Act;
    // It's legal iff the result and all operands are legal. Check return
    // value first since it's known as a vector value.
    if ((Act = TL->getTypeLegalizeAction(I.getType())) != Legal)
        return Act;
    // Check source operand.
    if ((Act = TL->getTypeLegalizeAction(I.getOperand(0)->getType())) != Legal)
        return Act;
    // Check the constant mask.
    return TL->getTypeLegalizeAction(I.getShuffleMaskForBitcode()->getType());
}

LegalizeAction InstLegalChecker::visitExtractValueInst(ExtractValueInst& I) {
    LegalizeAction Act;
    // It's legal iff the result and all operands are legal. Check return value
    // first.
    if ((Act = TL->getTypeLegalizeAction(I.getType())) != Legal)
        return Act;
    // Check aggregate operand.
    return TL->getTypeLegalizeAction(I.getAggregateOperand()->getType());
}

LegalizeAction InstLegalChecker::visitInsertValueInst(InsertValueInst& I) {
    LegalizeAction Act;
    // It's legal iff the result and all operands are legal. Check its return
    // value first since it's known as an aggregate value.
    if ((Act = TL->getTypeLegalizeAction(I.getType())) != Legal)
        return Act;
    // Check its operands.
    return TL->getTypeLegalizeAction(I.getInsertedValueOperand()->getType());
}

LegalizeAction InstLegalChecker::visitLandingPadInst(LandingPadInst&) {
    // FIXME: Do we support it?
    return Legal;
}

#if LLVM_VERSION_MAJOR >= 10
LegalizeAction InstLegalChecker::visitFNeg(llvm::UnaryOperator& I) {
    return Legal;
}
#endif
