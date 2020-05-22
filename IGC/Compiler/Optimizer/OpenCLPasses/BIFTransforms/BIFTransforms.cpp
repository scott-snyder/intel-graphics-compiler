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

#include "Compiler/Optimizer/OpenCLPasses/BIFTransforms/BIFTransforms.hpp"
#include "Compiler/IGCPassSupport.h"

#include "common/igc_regkeys.hpp"

#include "common/LLVMWarningsPush.hpp"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/StringRef.h"
#include "common/LLVMWarningsPop.hpp"

using namespace llvm;

namespace {

    class BIFTransforms : public ModulePass
    {
    public:
        static char ID;
        virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const
        {
            AU.setPreservesCFG();
        }

        BIFTransforms();

        ~BIFTransforms() {}

        virtual bool runOnModule(Module& M);

        virtual llvm::StringRef getPassName() const
        {
            return "BIFTransforms";
        }

    private:
        // Replace some BI Functions with faster versions, such as
        // length --> fast_length, etc.
        bool replaceBIF(Function& F);
    };

} // namespace

// Register pass to igc-opt
#define PASS_FLAG "igc-bif-transforms"
#define PASS_DESCRIPTION "Perform BIF-related transformations, such as replacing length with fast_length, etc"
#define PASS_CFG_ONLY false
#define PASS_ANALYSIS false
IGC_INITIALIZE_PASS_BEGIN(BIFTransforms, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)
IGC_INITIALIZE_PASS_END(BIFTransforms, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)


char BIFTransforms::ID = 0;

BIFTransforms::BIFTransforms() : ModulePass(ID)
{
    initializeBIFTransformsPass(*PassRegistry::getPassRegistry());
}

bool BIFTransforms::replaceBIF(Function& F)
{
    // replace
    //    length --> fast_length
    //    normalize --> fast_normaliez
    //    distance --> fast_distance
    // As the fast version only exists for float/double in BIF, not half,
    // here make sure F is not half before replacing.
    Function::arg_iterator AI = F.arg_begin();
    if (AI == F.arg_end())
    {
        return false;
    }
    Type* Ty = AI->getType();
    if (Ty->isHalfTy() ||
        (Ty->isVectorTy() && dyn_cast<VectorType>(Ty)->getElementType()->isHalfTy()))
    {
        return false;
    }

    bool changed = false;
    StringRef name = F.getName();
    if (name.startswith("_Z6length"))
    {   // length --> fast_length
        std::string newName("_Z11fast_length");
        newName.append(name.data() + 9);
        F.setName(newName);
        changed = true;
    }
    else if (name.startswith("_Z9normalize"))
    {   // normalize --> fast_normalize
        std::string newName("_Z14fast_normalize");
        newName.append(name.data() + 12);
        F.setName(newName);
        changed = true;
    }
    else if (name.startswith("_Z8distance"))
    {   // distance --> fast_distance
        std::string newName("_Z13fast_distance");
        newName.append(name.data() + 11);
        F.setName(newName);
        changed = true;
    }

    return changed;
}

bool BIFTransforms::runOnModule(Module& M)
{
    // OCL builtin uses SPIR name mangling (Itanium C++ ABI + extension)
    //  mangledName (n) = _Z<lengthof(n)><n><type>
    bool changed = false;
    for (Function& F : M)
    {
        if (F.isDeclaration())
        {
            if (IGC_IS_FLAG_ENABLED(EnableIntelFast) && replaceBIF(F))
            {
                changed = true;

            }
        }
    }
    return changed;
}

// Public interface to this pass
ModulePass* createBIFTransformsPass()
{
    return new BIFTransforms();
}
