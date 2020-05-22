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
#pragma once
#include "Compiler/CodeGenContextWrapper.hpp"
#include "common/MDFrameWork.h"

#include "common/LLVMWarningsPush.hpp"
#include <llvm/Pass.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/ConstantFolder.h>
#include "common/LLVMWarningsPop.hpp"

namespace llvm
{
    // Forward declare:
    class SampleIntrinsic;
}

namespace IGC
{
    class CustomSafeOptPass : public llvm::FunctionPass, public llvm::InstVisitor<CustomSafeOptPass>
    {
    public:
        static char ID;

        CustomSafeOptPass();

        ~CustomSafeOptPass() {}

        virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const override
        {
            AU.addRequired<CodeGenContextWrapper>();
            AU.setPreservesCFG();
        }

        virtual bool runOnFunction(llvm::Function& F) override;

        virtual llvm::StringRef getPassName() const override
        {
            return "Custom Pass Optimization";
        }

        void visitInstruction(llvm::Instruction& I);
        void visitAllocaInst(llvm::AllocaInst& I);
        void visitCallInst(llvm::CallInst& C);
        void visitBinaryOperator(llvm::BinaryOperator& I);
        bool isEmulatedAdd(llvm::BinaryOperator& I);
        void visitBfi(llvm::CallInst* inst);
        void visitf32tof16(llvm::CallInst* inst);
        void visitSampleBptr(llvm::SampleIntrinsic* inst);
        void visitMulH(llvm::CallInst* inst, bool isSigned);
        void visitFPToUIInst(llvm::FPToUIInst& FPUII);
        void visitFPTruncInst(llvm::FPTruncInst& I);
        void visitExtractElementInst(llvm::ExtractElementInst& I);
        void visitLdptr(llvm::CallInst* inst);
        void visitLoadInst(llvm::LoadInst& I);

        //
        // IEEE Floating point arithmetic is not associative.  Any pattern
        // match that changes the order or paramters is unsafe.
        //

        //
        // Removing sources is also unsafe.
        //  X * 1 => X     : Unsafe
        //  X + 0 => X     : Unsafe
        //  X - X => X     : Unsafe
        //

        // When in doubt assume a floating point optimization is unsafe!

        void visitBinaryOperatorTwoConstants(llvm::BinaryOperator& I);
        void visitBinaryOperatorPropNegate(llvm::BinaryOperator& I);
        void visitBitCast(llvm::BitCastInst& BC);

        void matchDp4a(llvm::BinaryOperator& I);

    private:
        bool psHasSideEffect;
    };

#if LLVM_VERSION_MAJOR >= 7
    class TrivialLocalMemoryOpsElimination : public llvm::FunctionPass, public llvm::InstVisitor<TrivialLocalMemoryOpsElimination>
    {
    public:
        static char ID;

        TrivialLocalMemoryOpsElimination();

        ~TrivialLocalMemoryOpsElimination() {}

        virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const override
        {
            AU.addRequired<CodeGenContextWrapper>();
            AU.setPreservesCFG();
        }

        virtual bool runOnFunction(llvm::Function& F) override;

        virtual llvm::StringRef getPassName() const override
        {
            return "TrivialLocalMemoryOpsElimination";
        }

        void visitLoadInst(llvm::LoadInst& I);
        void visitStoreInst(llvm::StoreInst& I);
        void visitCallInst(llvm::CallInst& I);
        bool isLocalBarrier(llvm::CallInst& I);
        void findNextThreadGroupBarrierInst(llvm::Instruction& I);
        void anyCallInstUseLocalMemory(llvm::CallInst& I);

    private:
        llvm::SmallVector<llvm::LoadInst*, 16> m_LocalLoadsToRemove;
        llvm::SmallVector<llvm::StoreInst*, 16> m_LocalStoresToRemove;
        llvm::SmallVector<llvm::CallInst*, 16> m_LocalFencesBariersToRemove;

        bool abortPass = false;
        const std::vector<bool> m_argumentsOfLocalMemoryBarrier{ true, false, false, false, false, false, true };
    };
#endif

    class GenSpecificPattern : public llvm::FunctionPass, public llvm::InstVisitor<GenSpecificPattern>
    {
    public:
        static char ID;

        GenSpecificPattern();

        ~GenSpecificPattern() {}

        virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const override
        {
            AU.setPreservesCFG();
            AU.addRequired<CodeGenContextWrapper>();
        }

        virtual bool runOnFunction(llvm::Function& F) override;

        virtual llvm::StringRef getPassName() const override
        {
            return "GenSpecificPattern";
        }

        void visitBinaryOperator(llvm::BinaryOperator& I);
        void visitSelectInst(llvm::SelectInst& I);
        void visitCmpInst(llvm::CmpInst& I);
        void visitZExtInst(llvm::ZExtInst& I);
        void visitCastInst(llvm::CastInst& I);
        void visitIntToPtr(llvm::IntToPtrInst& I);
        void visitSDiv(llvm::BinaryOperator& I);
        void visitTruncInst(llvm::TruncInst& I);
        void visitBitCastInst(llvm::BitCastInst& I);
#if LLVM_VERSION_MAJOR >= 10
        void visitFNeg(llvm::UnaryOperator& I);
#endif

        template <typename MaskType> void matchReverse(llvm::BinaryOperator& I);
        void createBitcastExtractInsertPattern(llvm::BinaryOperator& I,
            llvm::Value* Op1, llvm::Value* Op2, unsigned extractNum1, unsigned extractNum2);
    };

    class FCmpPaternMatch : public llvm::FunctionPass, public llvm::InstVisitor<FCmpPaternMatch>
    {
    public:
        static char ID;

        FCmpPaternMatch();

        ~FCmpPaternMatch() {}

        virtual bool runOnFunction(llvm::Function& F) override;

        virtual llvm::StringRef getPassName() const override
        {
            return "FCmpPaternMatch";
        }

        void visitSelectInst(llvm::SelectInst& I);
    };

#if 0
    class IGCConstantFolder : public llvm::ConstantFolder
    {
    public:
        IGCConstantFolder() :
            ConstantFolder()
        {}

        llvm::Constant* CreateCanonicalize(llvm::Constant* C0, bool flushDenorms = true) const;
        llvm::Constant* CreateFAdd(llvm::Constant* C0, llvm::Constant* C1, llvm::APFloatBase::roundingMode roundingMode) const;
        llvm::Constant* CreateFMul(llvm::Constant* C0, llvm::Constant* C1, llvm::APFloatBase::roundingMode roundingMode) const;
        llvm::Constant* CreateFPTrunc(llvm::Constant* C0, llvm::Type* dstType, llvm::APFloatBase::roundingMode roundingMode) const;
    };
#endif
class IGCConstantFolder final : public llvm::IRBuilderFolder {
  //virtual void anchor();

public:
  explicit IGCConstantFolder() = default;

  llvm::Constant* CreateCanonicalize(llvm::Constant* C0, bool flushDenorms = true) const;
  llvm::Constant* CreateFAdd(llvm::Constant* C0, llvm::Constant* C1, llvm::APFloatBase::roundingMode roundingMode) const;
  llvm::Constant* CreateFAdd(llvm::Constant* C0, llvm::Constant* C1) const
  { return CreateFAdd (C0, C1, llvm::APFloatBase::rmNearestTiesToEven); }
  llvm::Constant* CreateFMul(llvm::Constant* C0, llvm::Constant* C1, llvm::APFloatBase::roundingMode roundingMode) const;
  llvm::Constant* CreateFMul(llvm::Constant* C0, llvm::Constant* C1) const
  { return CreateFMul (C0, C1, llvm::APFloatBase::rmNearestTiesToEven); }
  llvm::Constant* CreateFPTrunc(llvm::Constant* C0, llvm::Type* dstType, llvm::APFloatBase::roundingMode roundingMode) const;

  //===--------------------------------------------------------------------===//
  // Binary Operators
  //===--------------------------------------------------------------------===//

  llvm::Constant *CreateAdd(llvm::Constant *LHS, llvm::Constant *RHS,
                      bool HasNUW = false, bool HasNSW = false) const override {
    return llvm::ConstantExpr::getAdd(LHS, RHS, HasNUW, HasNSW);
  }

#if 0
  llvm::Constant *CreateFAdd(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getFAdd(LHS, RHS);
  }
#endif

  llvm::Constant *CreateSub(llvm::Constant *LHS, llvm::Constant *RHS,
                      bool HasNUW = false, bool HasNSW = false) const override {
    return llvm::ConstantExpr::getSub(LHS, RHS, HasNUW, HasNSW);
  }

  llvm::Constant *CreateFSub(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getFSub(LHS, RHS);
  }

  llvm::Constant *CreateMul(llvm::Constant *LHS, llvm::Constant *RHS,
                      bool HasNUW = false, bool HasNSW = false) const override {
    return llvm::ConstantExpr::getMul(LHS, RHS, HasNUW, HasNSW);
  }

#if 0
  llvm::Constant *CreateFMul(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getFMul(LHS, RHS);
  }
#endif

  llvm::Constant *CreateUDiv(llvm::Constant *LHS, llvm::Constant *RHS,
                       bool isExact = false) const override {
    return llvm::ConstantExpr::getUDiv(LHS, RHS, isExact);
  }

  llvm::Constant *CreateSDiv(llvm::Constant *LHS, llvm::Constant *RHS,
                       bool isExact = false) const override {
    return llvm::ConstantExpr::getSDiv(LHS, RHS, isExact);
  }

  llvm::Constant *CreateFDiv(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getFDiv(LHS, RHS);
  }

  llvm::Constant *CreateURem(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getURem(LHS, RHS);
  }

  llvm::Constant *CreateSRem(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getSRem(LHS, RHS);
  }

  llvm::Constant *CreateFRem(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getFRem(LHS, RHS);
  }

  llvm::Constant *CreateShl(llvm::Constant *LHS, llvm::Constant *RHS,
                      bool HasNUW = false, bool HasNSW = false) const override {
    return llvm::ConstantExpr::getShl(LHS, RHS, HasNUW, HasNSW);
  }

  llvm::Constant *CreateLShr(llvm::Constant *LHS, llvm::Constant *RHS,
                       bool isExact = false) const override {
    return llvm::ConstantExpr::getLShr(LHS, RHS, isExact);
  }

  llvm::Constant *CreateAShr(llvm::Constant *LHS, llvm::Constant *RHS,
                       bool isExact = false) const override {
    return llvm::ConstantExpr::getAShr(LHS, RHS, isExact);
  }

  llvm::Constant *CreateAnd(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getAnd(LHS, RHS);
  }

  llvm::Constant *CreateOr(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getOr(LHS, RHS);
  }

  llvm::Constant *CreateXor(llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getXor(LHS, RHS);
  }

  llvm::Constant *CreateBinOp(llvm::Instruction::BinaryOps Opc,
                        llvm::Constant *LHS, llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::get(Opc, LHS, RHS);
  }

  //===--------------------------------------------------------------------===//
  // Unary Operators
  //===--------------------------------------------------------------------===//

  llvm::Constant *CreateNeg(llvm::Constant *C,
                      bool HasNUW = false, bool HasNSW = false) const override {
    return llvm::ConstantExpr::getNeg(C, HasNUW, HasNSW);
  }

  llvm::Constant *CreateFNeg(llvm::Constant *C) const override {
    return llvm::ConstantExpr::getFNeg(C);
  }

  llvm::Constant *CreateNot(llvm::Constant *C) const override {
    return llvm::ConstantExpr::getNot(C);
  }

  llvm::Constant *CreateUnOp(llvm::Instruction::UnaryOps Opc, llvm::Constant *C) const override {
    return llvm::ConstantExpr::get(Opc, C);
  }

  //===--------------------------------------------------------------------===//
  // Memory Instructions
  //===--------------------------------------------------------------------===//

  llvm::Constant *CreateGetElementPtr(llvm::Type *Ty, llvm::Constant *C,
                                llvm::ArrayRef<llvm::Constant *> IdxList) const override {
    return llvm::ConstantExpr::getGetElementPtr(Ty, C, IdxList);
  }

  llvm::Constant *CreateGetElementPtr(llvm::Type *Ty, llvm::Constant *C,
                                llvm::Constant *Idx) const override {
    // This form of the function only exists to avoid ambiguous overload
    // warnings about whether to convert Idx to llvm::ArrayRef<llvm::Constant *> or
    // llvm::ArrayRef<Value *>.
    return llvm::ConstantExpr::getGetElementPtr(Ty, C, Idx);
  }

  llvm::Constant *CreateGetElementPtr(llvm::Type *Ty, llvm::Constant *C,
                                llvm::ArrayRef<llvm::Value *> IdxList) const override {
    return llvm::ConstantExpr::getGetElementPtr(Ty, C, IdxList);
  }

  llvm::Constant *CreateInBoundsGetElementPtr(
                                        llvm::Type *Ty, llvm::Constant *C, llvm::ArrayRef<llvm::Constant *> IdxList) const override {
    return llvm::ConstantExpr::getInBoundsGetElementPtr(Ty, C, IdxList);
  }

  llvm::Constant *CreateInBoundsGetElementPtr(llvm::Type *Ty, llvm::Constant *C,
                                        llvm::Constant *Idx) const override {
    // This form of the function only exists to avoid ambiguous overload
    // warnings about whether to convert Idx to llvm::ArrayRef<llvm::Constant *> or
    // llvm::ArrayRef<llvm::Value *>.
    return llvm::ConstantExpr::getInBoundsGetElementPtr(Ty, C, Idx);
  }

  llvm::Constant *CreateInBoundsGetElementPtr(
                                        llvm::Type *Ty, llvm::Constant *C, llvm::ArrayRef<llvm::Value *> IdxList) const override {
    return llvm::ConstantExpr::getInBoundsGetElementPtr(Ty, C, IdxList);
  }

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  llvm::Constant *CreateCast(llvm::Instruction::CastOps Op, llvm::Constant *C,
                       llvm::Type *DestTy) const override {
    return llvm::ConstantExpr::getCast(Op, C, DestTy);
  }

  llvm::Constant *CreatePointerCast(llvm::Constant *C, llvm::Type *DestTy) const override {
    return llvm::ConstantExpr::getPointerCast(C, DestTy);
  }

  llvm::Constant *CreatePointerBitCastOrAddrSpaceCast(llvm::Constant *C,
                                                llvm::Type *DestTy) const override {
    return llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(C, DestTy);
  }

  llvm::Constant *CreateIntCast(llvm::Constant *C, llvm::Type *DestTy,
                          bool isSigned) const override {
    return llvm::ConstantExpr::getIntegerCast(C, DestTy, isSigned);
  }

  llvm::Constant *CreateFPCast(llvm::Constant *C, llvm::Type *DestTy) const override {
    return llvm::ConstantExpr::getFPCast(C, DestTy);
  }

  llvm::Constant *CreateBitCast(llvm::Constant *C, llvm::Type *DestTy) const override {
    return CreateCast(llvm::Instruction::BitCast, C, DestTy);
  }

  llvm::Constant *CreateIntToPtr(llvm::Constant *C, llvm::Type *DestTy) const override {
    return CreateCast(llvm::Instruction::IntToPtr, C, DestTy);
  }

  llvm::Constant *CreatePtrToInt(llvm::Constant *C, llvm::Type *DestTy) const override {
    return CreateCast(llvm::Instruction::PtrToInt, C, DestTy);
  }

  llvm::Constant *CreateZExtOrBitCast(llvm::Constant *C, llvm::Type *DestTy) const override {
    return llvm::ConstantExpr::getZExtOrBitCast(C, DestTy);
  }

  llvm::Constant *CreateSExtOrBitCast(llvm::Constant *C, llvm::Type *DestTy) const override {
    return llvm::ConstantExpr::getSExtOrBitCast(C, DestTy);
  }

  llvm::Constant *CreateTruncOrBitCast(llvm::Constant *C, llvm::Type *DestTy) const override {
    return llvm::ConstantExpr::getTruncOrBitCast(C, DestTy);
  }

  //===--------------------------------------------------------------------===//
  // Compare Instructions
  //===--------------------------------------------------------------------===//

  llvm::Constant *CreateICmp(llvm::CmpInst::Predicate P, llvm::Constant *LHS,
                       llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getCompare(P, LHS, RHS);
  }

  llvm::Constant *CreateFCmp(llvm::CmpInst::Predicate P, llvm::Constant *LHS,
                       llvm::Constant *RHS) const override {
    return llvm::ConstantExpr::getCompare(P, LHS, RHS);
  }

  //===--------------------------------------------------------------------===//
  // Other Instructions
  //===--------------------------------------------------------------------===//

  llvm::Constant *CreateSelect(llvm::Constant *C, llvm::Constant *True,
                         llvm::Constant *False) const override {
    return llvm::ConstantExpr::getSelect(C, True, False);
  }

  llvm::Constant *CreateExtractElement(llvm::Constant *Vec, llvm::Constant *Idx) const override {
    return llvm::ConstantExpr::getExtractElement(Vec, Idx);
  }

  llvm::Constant *CreateInsertElement(llvm::Constant *Vec, llvm::Constant *NewElt,
                                llvm::Constant *Idx) const override {
    return llvm::ConstantExpr::getInsertElement(Vec, NewElt, Idx);
  }

  llvm::Constant *CreateShuffleVector(llvm::Constant *V1, llvm::Constant *V2,
                                llvm::ArrayRef<int> Mask) const override {
    return llvm::ConstantExpr::getShuffleVector(V1, V2, Mask);
  }

  llvm::Constant *CreateExtractValue(llvm::Constant *Agg,
                               llvm::ArrayRef<unsigned> IdxList) const override {
    return llvm::ConstantExpr::getExtractValue(Agg, IdxList);
  }

  llvm::Constant *CreateInsertValue(llvm::Constant *Agg, llvm::Constant *Val,
                              llvm::ArrayRef<unsigned> IdxList) const override {
    return llvm::ConstantExpr::getInsertValue(Agg, Val, IdxList);
  }
};

    class IGCConstProp : public llvm::FunctionPass
    {
    public:
        static char ID;

        IGCConstProp(bool enableMathConstProp = false,
            bool enableSimplifyGEP = false);

        ~IGCConstProp() {}

        virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const override
        {
            AU.addRequired<llvm::TargetLibraryInfoWrapperPass>();
            AU.addRequired<CodeGenContextWrapper>();
            AU.setPreservesCFG();
        }

        virtual bool runOnFunction(llvm::Function& F) override;

        virtual llvm::StringRef getPassName() const override
        {
            // specialized const-prop with shader-const replacement
            return "const-prop with shader-const replacement";
        }

    private:
        llvm::Module* module;
        llvm::Constant* ReplaceFromDynConstants(unsigned bufId, unsigned eltId, unsigned int size_in_bytes, llvm::LoadInst* inst);
        llvm::Constant* replaceShaderConstant(llvm::LoadInst* inst);
        llvm::Constant* ConstantFoldCmpInst(llvm::CmpInst* inst);
        llvm::Constant* ConstantFoldExtractElement(llvm::ExtractElementInst* inst);
        llvm::Constant* ConstantFoldCallInstruction(llvm::CallInst* inst);
        bool simplifyAdd(llvm::BinaryOperator* BO);
        bool simplifyGEP(llvm::GetElementPtrInst* GEP);
        bool m_enableMathConstProp;
        bool m_enableSimplifyGEP;
        const llvm::DataLayout* m_TD;
        llvm::TargetLibraryInfo* m_TLI;
    };

    llvm::FunctionPass* createGenStrengthReductionPass();
    llvm::FunctionPass* createNanHandlingPass();
    llvm::FunctionPass* createFlattenSmallSwitchPass();
    llvm::FunctionPass* createIGCIndirectICBPropagaionPass();
    llvm::FunctionPass* createBlendToDiscardPass();
    llvm::FunctionPass* createMarkReadOnlyLoadPass();
    llvm::FunctionPass* createLogicalAndToBranchPass();

} // namespace IGC
