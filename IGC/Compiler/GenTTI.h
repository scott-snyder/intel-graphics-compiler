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
#include "Compiler/CodeGenPublic.h"
#include "common/LLVMWarningsPush.hpp"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfoImpl.h"
#include "common/LLVMWarningsPop.hpp"

namespace llvm
{
    class DummyPass : public ImmutablePass
    {
    public:
        static char ID;
        DummyPass();
    };

    // This implementation allows us to define our own costs for the GenIntrinsics
    // Did not use BasicTTIImplBase because the overloaded constructors have TragetMachine as an argument,
    // so I inherited from its parent which has only DL as its arguments
    class GenIntrinsicsTTIImpl : public TargetTransformInfoImplCRTPBase<GenIntrinsicsTTIImpl>
    {
        typedef TargetTransformInfoImplCRTPBase<GenIntrinsicsTTIImpl> BaseT;
        typedef TargetTransformInfo TTI;
        friend BaseT;
        IGC::CodeGenContext* ctx;
        DummyPass* dummyPass;
    public:
        GenIntrinsicsTTIImpl(IGC::CodeGenContext* pCtx, DummyPass* pDummyPass) :
            BaseT(pCtx->getModule()->getDataLayout()), ctx(pCtx) {
            dummyPass = pDummyPass;
        }

        bool shouldBuildLookupTables();

        bool isLoweredToCall(const Function* F);

        void* getAdjustedAnalysisPointer(const void* ID);

        void getUnrollingPreferences(Loop* L,
#if LLVM_VERSION_MAJOR >= 7
            ScalarEvolution & SE,
#endif
            TTI::UnrollingPreferences & UP);

        bool isProfitableToHoist(Instruction* I);

      //using BaseT::getCallCost;
        unsigned getCallCost(const Function* F, ArrayRef<const Value*> Args
#if LLVM_VERSION_MAJOR >= 9
            , const User * U
#endif
        );
    };

}
