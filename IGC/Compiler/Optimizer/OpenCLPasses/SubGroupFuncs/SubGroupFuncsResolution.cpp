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

#include "Compiler/Optimizer/OpenCLPasses/SubGroupFuncs/SubGroupFuncsResolution.hpp"
#include "Compiler/Optimizer/OpenCLPasses/KernelArgs.hpp"
#include "Compiler/IGCPassSupport.h"
#include "common/LLVMWarningsPush.hpp"
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include "common/LLVMWarningsPop.hpp"
#include "Probe/Assertion.h"

using namespace llvm;
using namespace IGC;
using namespace IGC::IGCMD;

// Register pass to igc-opt
#define PASS_FLAG "igc-sub-group-func-resolution"
#define PASS_DESCRIPTION "Resolves sub group functions"
#define PASS_CFG_ONLY false
#define PASS_ANALYSIS false
IGC_INITIALIZE_PASS_BEGIN(SubGroupFuncsResolution, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)
IGC_INITIALIZE_PASS_DEPENDENCY(CodeGenContextWrapper)
IGC_INITIALIZE_PASS_DEPENDENCY(MetaDataUtilsWrapper)
IGC_INITIALIZE_PASS_END(SubGroupFuncsResolution, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)

char SubGroupFuncsResolution::ID = 0;

const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_BARRIER = "__builtin_IB_sub_group_barrier";
const llvm::StringRef SubGroupFuncsResolution::GET_MAX_SUB_GROUP_SIZE = "__builtin_IB_get_simd_size";
const llvm::StringRef SubGroupFuncsResolution::GET_SUB_GROUP_LOCAL_ID = "__builtin_IB_get_simd_id";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE = "__builtin_IB_simd_shuffle";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_B = "__builtin_IB_simd_shuffle_b";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_C = "__builtin_IB_simd_shuffle_c";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_US = "__builtin_IB_simd_shuffle_us";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_F = "__builtin_IB_simd_shuffle_f";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_H = "__builtin_IB_simd_shuffle_h";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_DF = "__builtin_IB_simd_shuffle_df";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_DOWN = "__builtin_IB_simd_shuffle_down";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_DOWN_US = "__builtin_IB_simd_shuffle_down_us";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_DOWN_UC = "__builtin_IB_simd_shuffle_down_uc";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_1_GBL = "__builtin_IB_simd_block_read_1_global";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_2_GBL = "__builtin_IB_simd_block_read_2_global";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_4_GBL = "__builtin_IB_simd_block_read_4_global";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_8_GBL = "__builtin_IB_simd_block_read_8_global";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_1_GBL_H = "__builtin_IB_simd_block_read_1_global_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_2_GBL_H = "__builtin_IB_simd_block_read_2_global_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_4_GBL_H = "__builtin_IB_simd_block_read_4_global_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_8_GBL_H = "__builtin_IB_simd_block_read_8_global_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_16_GBL_H = "__builtin_IB_simd_block_read_16_global_h";

const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_1_GBL_B = "__builtin_IB_simd_block_read_1_global_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_2_GBL_B = "__builtin_IB_simd_block_read_2_global_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_4_GBL_B = "__builtin_IB_simd_block_read_4_global_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_8_GBL_B = "__builtin_IB_simd_block_read_8_global_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_16_GBL_B = "__builtin_IB_simd_block_read_16_global_b";

const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_1_GBL_L = "__builtin_IB_simd_block_read_1_global_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_2_GBL_L = "__builtin_IB_simd_block_read_2_global_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_4_GBL_L = "__builtin_IB_simd_block_read_4_global_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_8_GBL_L = "__builtin_IB_simd_block_read_8_global_l";

const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_GBL = "__builtin_IB_simd_block_write_1_global";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_GBL = "__builtin_IB_simd_block_write_2_global";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_GBL = "__builtin_IB_simd_block_write_4_global";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_GBL = "__builtin_IB_simd_block_write_8_global";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_GBL_H = "__builtin_IB_simd_block_write_1_global_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_GBL_H = "__builtin_IB_simd_block_write_2_global_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_GBL_H = "__builtin_IB_simd_block_write_4_global_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_GBL_H = "__builtin_IB_simd_block_write_8_global_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_16_GBL_H = "__builtin_IB_simd_block_write_16_global_h";

const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_GBL_B = "__builtin_IB_simd_block_write_1_global_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_GBL_B = "__builtin_IB_simd_block_write_2_global_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_GBL_B = "__builtin_IB_simd_block_write_4_global_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_GBL_B = "__builtin_IB_simd_block_write_8_global_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_16_GBL_B = "__builtin_IB_simd_block_write_16_global_b";

const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_GBL_L = "__builtin_IB_simd_block_write_1_global_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_GBL_L = "__builtin_IB_simd_block_write_2_global_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_GBL_L = "__builtin_IB_simd_block_write_4_global_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_GBL_L = "__builtin_IB_simd_block_write_8_global_l";

const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_1_LCL = "__builtin_IB_simd_block_read_1_local";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_2_LCL = "__builtin_IB_simd_block_read_2_local";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_4_LCL = "__builtin_IB_simd_block_read_4_local";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_8_LCL = "__builtin_IB_simd_block_read_8_local";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_1_LCL_H = "__builtin_IB_simd_block_read_1_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_2_LCL_H = "__builtin_IB_simd_block_read_2_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_4_LCL_H = "__builtin_IB_simd_block_read_4_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_8_LCL_H = "__builtin_IB_simd_block_read_8_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_16_LCL_H = "__builtin_IB_simd_block_read_16_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_1_LCL_B = "__builtin_IB_simd_block_read_1_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_2_LCL_B = "__builtin_IB_simd_block_read_2_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_4_LCL_B = "__builtin_IB_simd_block_read_4_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_8_LCL_B = "__builtin_IB_simd_block_read_8_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_16_LCL_B = "__builtin_IB_simd_block_read_16_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_1_LCL_L = "__builtin_IB_simd_block_read_1_local_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_2_LCL_L = "__builtin_IB_simd_block_read_2_local_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_4_LCL_L = "__builtin_IB_simd_block_read_4_local_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_READ_8_LCL_L = "__builtin_IB_simd_block_read_8_local_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_LCL = "__builtin_IB_simd_block_write_1_local";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_LCL = "__builtin_IB_simd_block_write_2_local";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_LCL = "__builtin_IB_simd_block_write_4_local";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_LCL = "__builtin_IB_simd_block_write_8_local";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_LCL_H = "__builtin_IB_simd_block_write_1_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_LCL_H = "__builtin_IB_simd_block_write_2_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_LCL_H = "__builtin_IB_simd_block_write_4_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_LCL_H = "__builtin_IB_simd_block_write_8_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_16_LCL_H = "__builtin_IB_simd_block_write_16_local_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_LCL_B = "__builtin_IB_simd_block_write_1_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_LCL_B = "__builtin_IB_simd_block_write_2_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_LCL_B = "__builtin_IB_simd_block_write_4_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_LCL_B = "__builtin_IB_simd_block_write_8_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_16_LCL_B = "__builtin_IB_simd_block_write_16_local_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_LCL_L = "__builtin_IB_simd_block_write_1_local_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_LCL_L = "__builtin_IB_simd_block_write_2_local_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_LCL_L = "__builtin_IB_simd_block_write_4_local_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_LCL_L = "__builtin_IB_simd_block_write_8_local_l";

const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_1 = "__builtin_IB_simd_media_block_read_1";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_2 = "__builtin_IB_simd_media_block_read_2";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_4 = "__builtin_IB_simd_media_block_read_4";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_8 = "__builtin_IB_simd_media_block_read_8";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_1_H = "__builtin_IB_simd_media_block_read_1_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_2_H = "__builtin_IB_simd_media_block_read_2_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_4_H = "__builtin_IB_simd_media_block_read_4_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_8_H = "__builtin_IB_simd_media_block_read_8_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_16_H = "__builtin_IB_simd_media_block_read_16_h";

const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_1_B = "__builtin_IB_simd_media_block_read_1_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_2_B = "__builtin_IB_simd_media_block_read_2_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_4_B = "__builtin_IB_simd_media_block_read_4_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_8_B = "__builtin_IB_simd_media_block_read_8_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_16_B = "__builtin_IB_simd_media_block_read_16_b";

const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_1_L = "__builtin_IB_simd_media_block_read_1_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_2_L = "__builtin_IB_simd_media_block_read_2_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_4_L = "__builtin_IB_simd_media_block_read_4_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_8_L = "__builtin_IB_simd_media_block_read_8_l";

const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_1 = "__builtin_IB_simd_media_block_write_1";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_2 = "__builtin_IB_simd_media_block_write_2";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_4 = "__builtin_IB_simd_media_block_write_4";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_8 = "__builtin_IB_simd_media_block_write_8";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_1_H = "__builtin_IB_simd_media_block_write_1_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_2_H = "__builtin_IB_simd_media_block_write_2_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_4_H = "__builtin_IB_simd_media_block_write_4_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_8_H = "__builtin_IB_simd_media_block_write_8_h";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_16_H = "__builtin_IB_simd_media_block_write_16_h";

const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_1_L = "__builtin_IB_simd_media_block_write_1_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_2_L = "__builtin_IB_simd_media_block_write_2_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_4_L = "__builtin_IB_simd_media_block_write_4_l";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_8_L = "__builtin_IB_simd_media_block_write_8_l";

const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_1_B = "__builtin_IB_simd_media_block_write_1_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_2_B = "__builtin_IB_simd_media_block_write_2_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_4_B = "__builtin_IB_simd_media_block_write_4_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_8_B = "__builtin_IB_simd_media_block_write_8_b";
const llvm::StringRef SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_16_B = "__builtin_IB_simd_media_block_write_16_b";

const llvm::StringRef SubGroupFuncsResolution::MEDIA_BLOCK_READ = "__builtin_IB_media_block_read";
const llvm::StringRef SubGroupFuncsResolution::MEDIA_BLOCK_WRITE = "__builtin_IB_media_block_write";

const llvm::StringRef SubGroupFuncsResolution::MEDIA_BLOCK_RECTANGLE_READ = "__builtin_IB_media_block_rectangle_read";
const llvm::StringRef SubGroupFuncsResolution::GET_IMAGE_BTI = "__builtin_IB_get_image_bti";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_REDUCE = "__builtin_IB_sub_group_reduce";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_SCAN = "__builtin_IB_sub_group_scan";
const llvm::StringRef SubGroupFuncsResolution::SUB_GROUP_CLUSTERED_REDUCE = "__builtin_IB_sub_group_clustered_reduce";

const std::array<std::pair<std::string, WaveOps>, 13> SubGroupFuncsResolution::m_spvOpToWaveOpMap =
{
    {
        {"IAdd", WaveOps::SUM},
        {"FAdd", WaveOps::FSUM},
        {"SMax", WaveOps::IMAX},
        {"UMax", WaveOps::UMAX},
        {"FMax", WaveOps::FMAX},
        {"SMin", WaveOps::IMIN},
        {"UMin", WaveOps::UMIN},
        {"FMin", WaveOps::FMIN},
        {"IMul", WaveOps::PROD},
        {"FMul", WaveOps::FPROD},
        {"And", WaveOps::AND},
        {"Or", WaveOps::OR},
        {"Xor", WaveOps::XOR}
    }
};



SubGroupFuncsResolution::SubGroupFuncsResolution(void) : FunctionPass(ID)
{
    initializeSubGroupFuncsResolutionPass(*PassRegistry::getPassRegistry());
}

bool SubGroupFuncsResolution::runOnFunction(Function& F)
{
    m_pCtx = getAnalysis<CodeGenContextWrapper>().getCodeGenContext();

    m_argIndexMap.clear();
    m_instsToDelete.clear();
    m_changed = false;

    visit(F);

    for (Instruction* inst : m_instsToDelete) {
        inst->eraseFromParent();
    }

    return m_changed;
}

// Debug line info helper function
static void updateDebugLoc(Instruction* pOrigin, Instruction* pNew)
{
    IGC_ASSERT(pOrigin && pNew && "Expect valid instructions");
    pNew->setDebugLoc(pOrigin->getDebugLoc());
}

// Helps to obtain temporary index corresponding to the kernel argument.
// This index will be used during codegen to resolve BTIs for Images (SRVs and UAVs).
void SubGroupFuncsResolution::BTIHelper(llvm::CallInst& CI)
{
    Function* F = CI.getParent()->getParent();
    ModuleMetaData* modMD = getAnalysis<MetaDataUtilsWrapper>().getModuleMetaData();

    for (Function::arg_iterator arg = F->arg_begin(), e = F->arg_end(); arg != e; ++arg)
    {
        int argNo = (*arg).getArgNo();
        FunctionMetaData* funcMD = &modMD->FuncMD[F];
        ResourceAllocMD* resAllocMD = &funcMD->resAllocMD;
        IGC_ASSERT((size_t)argNo < resAllocMD->argAllocMDList.size() && "ArgAllocMD List Out of Bounds");
        ArgAllocMD* argAlloc = &resAllocMD->argAllocMDList[argNo];
        m_argIndexMap[&(*arg)] = CImagesBI::ParamInfo(
            argAlloc->indexType,
            (ResourceTypeEnum)argAlloc->type,
            (ResourceExtensionTypeEnum)argAlloc->extensionType);
    }
}

int32_t SubGroupFuncsResolution::GetSIMDSize(Function* F)
{
    auto* pMdUtils = getAnalysis<MetaDataUtilsWrapper>().getMetaDataUtils();
    auto funcInfoMD = pMdUtils->getFunctionsInfoItem(F);
    int32_t simdSize = funcInfoMD->getSubGroupSize()->getSIMD_size();

    return simdSize;
}

void SubGroupFuncsResolution::CheckSIMDSize(Instruction& I, StringRef msg)
{
    int32_t simdSize = GetSIMDSize(I.getParent()->getParent());

    if ((simdSize == 32 || m_pCtx->getModuleMetaData()->csInfo.forcedSIMDSize == 32)
        )
    {
        m_pCtx->EmitError(std::string(msg).c_str());
    }
}

WaveOps SubGroupFuncsResolution::GetWaveOp(StringRef funcName)
{
    for (auto op : SubGroupFuncsResolution::m_spvOpToWaveOpMap)
    {
        if (funcName.contains(op.first))
        {
            return op.second;
        }
    }
    IGC_ASSERT(false && "Function name does not contain spir-v operation type");
    return WaveOps::UNDEF;
}

void SubGroupFuncsResolution::mediaBlockRead(llvm::CallInst& CI)
{
    // Creates intrinsics that will be lowered in the CodeGen and will handle the simd_media_block_read
    SmallVector<Value*, 5> args;
    pushMediaBlockArgs(args, CI);

    // Check if the only use of CI is conversion to float. If so, use float version of intrinsic and remove the cast instruction.

    Value* use = NULL;
    if (CI.hasOneUse())
    {
        use = *(CI.user_begin());
    }

    if (use && isa<BitCastInst>(use) && (use->getType()->getScalarType()->isFloatTy() || use->getType()->getScalarType()->isHalfTy()))
    {
        BitCastInst* bitCast = cast<BitCastInst>(use);
        Function* simdMediaBlockReadFunc = GenISAIntrinsic::getDeclaration(
            CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_simdMediaBlockRead,
            use->getType());
        Instruction* simdMediaBlockRead = CallInst::Create(simdMediaBlockReadFunc, args, "", &CI);
        use->replaceAllUsesWith(simdMediaBlockRead);
        m_instsToDelete.push_back(bitCast);
        m_instsToDelete.push_back(&CI);
    }
    else {
        Function* simdMediaBlockReadFunc = GenISAIntrinsic::getDeclaration(
            CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_simdMediaBlockRead,
            CI.getType());
        Instruction* simdMediaBlockRead = CallInst::Create(simdMediaBlockReadFunc, args, "", &CI);
        CI.replaceAllUsesWith(simdMediaBlockRead);
        CI.eraseFromParent();
    }

}

void SubGroupFuncsResolution::mediaBlockWrite(llvm::CallInst& CI)
{
    SmallVector<Value*, 5> args;
    pushMediaBlockArgs(args, CI);
    args.push_back(CI.getArgOperand(2)); // push data

    Function* simdMediaBlockWriteFunc = GenISAIntrinsic::getDeclaration(
        CI.getCalledFunction()->getParent(),
        GenISAIntrinsic::GenISA_simdMediaBlockWrite,
        CI.getArgOperand(2)->getType());
    Instruction* simdMediaBlockWrite = CallInst::Create(simdMediaBlockWriteFunc, args, "", &CI);

    CI.replaceAllUsesWith(simdMediaBlockWrite);
    CI.eraseFromParent();
}

void SubGroupFuncsResolution::simdBlockRead(llvm::CallInst& CI)
{
    // Creates intrinsics that will be lowered in the CodeGen and will handle the simd_block_read
    LLVMContext& C = CI.getCalledFunction()->getContext();
    Value* Ptr = CI.getArgOperand(0);
    PointerType* PtrTy = dyn_cast<PointerType>(Ptr->getType());
    IGC_ASSERT(PtrTy && "simdBlockRead has non-pointer type!");
    SmallVector<Value*, 1> args;
    args.push_back(Ptr);
    SmallVector<Type*, 2>  types;
    types.push_back(nullptr); types.push_back(nullptr);
    GenISAIntrinsic::ID  genIntrinID = GenISAIntrinsic::GenISA_simdBlockRead;
    ADDRESS_SPACE AS = (ADDRESS_SPACE)PtrTy->getAddressSpace();
    bool supportLocal = false;
    supportLocal = m_pCtx->platform.supportSLMBlockMessage();
    if (AS == ADDRESS_SPACE_LOCAL && !supportLocal)
    {
        m_pCtx->EmitError("BlockReadLocal not supported!");
        return;
    }

    unsigned int scalarSizeInBits = CI.getType()->getScalarType()->getScalarSizeInBits();

    switch (scalarSizeInBits)
    {
    case 8:
        types[1] = Type::getInt8PtrTy(C, AS);
        break;
    case 16:
        types[1] = Type::getInt16PtrTy(C, AS);
        break;
    case 64:
        types[1] = (Type::getInt64PtrTy(C, AS));
        break;
    default:
        IGC_ASSERT(false && "unrecognized bit width!");
        // assertion failed but continue code failsafe using default 32
    case 32:
        types[1] = (Type::getInt32PtrTy(C, AS));
        break;
    }

    // Check if the only use of CI is conversion to float. If so, use float version of intrinsic and remove the cast instruction.

    Value* use = NULL;
    if (CI.hasOneUse())
    {
        use = *(CI.user_begin());
    }

    if (use && isa<BitCastInst>(use) &&
        ((use->getType()->getScalarType()->isFloatTy() && scalarSizeInBits == 32) ||
            (use->getType()->getScalarType()->isDoubleTy() && scalarSizeInBits == 64)))
    {
        BitCastInst* bitCast = cast<BitCastInst>(use);
        types[0] = bitCast->getType();
        Function* simdBlockReadFunc = GenISAIntrinsic::getDeclaration(
            CI.getCalledFunction()->getParent(),
            genIntrinID,
            types);
        Instruction* simdBlockRead = CallInst::Create(simdBlockReadFunc, args, "", &CI);
        use->replaceAllUsesWith(simdBlockRead);
        m_instsToDelete.push_back(bitCast);
        m_instsToDelete.push_back(&CI);
    }
    else {
        types[0] = CI.getType();
        Function* simdBlockReadFunc = GenISAIntrinsic::getDeclaration(
            CI.getCalledFunction()->getParent(),
            genIntrinID,
            types);
        Instruction* simdBlockRead = CallInst::Create(simdBlockReadFunc, args, "", &CI);
        CI.replaceAllUsesWith(simdBlockRead);
        CI.eraseFromParent();
    }
}

void SubGroupFuncsResolution::simdBlockWrite(llvm::CallInst& CI)
{
    LLVMContext& C = CI.getCalledFunction()->getContext();
    Value* Ptr = CI.getArgOperand(0);
    PointerType* PtrTy = dyn_cast<PointerType>(Ptr->getType());
    IGC_ASSERT(PtrTy && "simdBlockWrite has non-pointer type!");
    ADDRESS_SPACE AS = (ADDRESS_SPACE)PtrTy->getAddressSpace();
    bool supportLocal = false;
    supportLocal = m_pCtx->platform.supportSLMBlockMessage();
    if (AS == ADDRESS_SPACE_LOCAL && !supportLocal)
    {
        m_pCtx->EmitError("BlockWriteLocal not supported!");
        return;
    }

    SmallVector<Value*, 2> args;
    SmallVector<Type*, 2>  types;
    Value* dataArg = CI.getArgOperand(1);

    args.push_back(CI.getArgOperand(0));
    args.push_back(dataArg);

    switch (dataArg->getType()->getScalarType()->getScalarSizeInBits())
    {
    case 8:
        types.push_back(Type::getInt8PtrTy(C, AS));
        break;
    case 16:
        types.push_back(Type::getInt16PtrTy(C, AS));
        break;
    case 64:
        types.push_back(Type::getInt64PtrTy(C, AS));
        break;
    default:
        IGC_ASSERT(false && "unrecognized bit width!");
        // assertion failed but continue code failsafe using default 32
    case 32:
        types.push_back(Type::getInt32PtrTy(C, AS));
        break;
    }

    types.push_back(dataArg->getType());
    Function* simdBlockWriteFunc = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(),
        GenISAIntrinsic::GenISA_simdBlockWrite, types);
    Instruction* simdBlockWrite = CallInst::Create(simdBlockWriteFunc, args, "", &CI);

    CI.replaceAllUsesWith(simdBlockWrite);
    CI.eraseFromParent();
}

void SubGroupFuncsResolution::pushMediaBlockArgs(llvm::SmallVector<llvm::Value*, 5> & args, llvm::CallInst& CI)
{
    LLVMContext& C = CI.getCalledFunction()->getContext();

    if (m_argIndexMap.empty())
    {
        BTIHelper(CI);
    }

    Argument* pImg = nullptr;
    ConstantInt* imageIndex = IGC::CImagesBI::CImagesUtils::getImageIndex(&m_argIndexMap, &CI, 0, pImg);

    ConstantInt* constIndex = ConstantInt::get((Type::getInt32Ty(C)), 0);
    Instruction* xOffset = ExtractElementInst::Create(CI.getArgOperand(1), constIndex, "xOffset", &CI);

    ConstantInt* constIndex2 = ConstantInt::get((Type::getInt32Ty(C)), 1);
    Instruction* yOffset = ExtractElementInst::Create(CI.getArgOperand(1), constIndex2, "yOffset", &CI);

    BufferType   imageType = IGC::CImagesBI::CImagesUtils::getImageType(&m_argIndexMap, &CI, 0);
    uint32_t     isUAV = imageType == UAV ? 1 : 0;
    ConstantInt* isImageTypeUAV = ConstantInt::get((Type::getInt32Ty(C)), isUAV);

    updateDebugLoc(&CI, xOffset);
    updateDebugLoc(&CI, yOffset);

    args.push_back(imageIndex);
    args.push_back(xOffset);
    args.push_back(yOffset);
    args.push_back(isImageTypeUAV);
}

void SubGroupFuncsResolution::subGroupArithmetic(CallInst& CI, WaveOps op, GroupOpType groupType)
{
    IRBuilder<> IRB(&CI);
    Value* arg = CI.getArgOperand(0);
    // GenISA_Wave* instrinsics do not support i1 type. Handle this with i8 version of instrinsic.
    bool isBoolean = (arg->getType() == IRB.getInt1Ty());
    if (isBoolean)
    {
        arg = IRB.CreateZExt(arg, IRB.getInt8Ty());
    }
    Value* opVal = IRB.getInt8((uint8_t)op);
    Value* waveCall = nullptr;
    if (groupType == GroupOperationReduce)
    {
        Value* args[2] = { arg, opVal };
        Function* waveAll = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_WaveAll,
            arg->getType());
        waveCall = IRB.CreateCall(waveAll, args);
    }
    else if (groupType == GroupOperationScan)
    {
        Value* args[4] = { arg, opVal, IRB.getInt1(false), IRB.getInt1(true) };
        Function* waveScan = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_WavePrefix,
            arg->getType());
        waveCall = IRB.CreateCall(waveScan, args);
    }
    else if (groupType == GroupOperationClusteredReduce)
    {
        Value* clusterSize = CI.getOperand(1);
        Value* args[3] = { arg, opVal, clusterSize };
        Function* waveClustered = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_WaveClustered,
            arg->getType());
        waveCall = IRB.CreateCall(waveClustered, args);
    }
    else
    {
        IGC_ASSERT(false && "Unsupported group operation type!");
    }

    if (isBoolean)
    {
        waveCall = IRB.CreateTrunc(waveCall, IRB.getInt1Ty());
    }
    CI.replaceAllUsesWith(waveCall);
    CI.eraseFromParent();
}

void SubGroupFuncsResolution::visitCallInst(CallInst& CI)
{
    Function* func = CI.getCalledFunction();
    if (!func)
        return;
    StringRef funcName = func->getName();
    LLVMContext& Ctx = CI.getCalledFunction()->getContext();

    if (funcName.equals(SubGroupFuncsResolution::GET_MAX_SUB_GROUP_SIZE))
    {
        int32_t simdSize = GetSIMDSize(CI.getParent()->getParent());
        if (simdSize == 8 || simdSize == 16 || simdSize == 32)
        {
            auto* C = ConstantInt::get(Type::getInt32Ty(Ctx), simdSize);
            CI.replaceAllUsesWith(C);
        }
        else
        {
            // Creates intrinsics that will be lowered in the CodeGen and will handle the sub_group size
            Function* simdSizeFunc = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(), GenISAIntrinsic::GenISA_simdSize);
            Instruction* simdSize = CallInst::Create(simdSizeFunc, "simdSize", &CI);
            CI.replaceAllUsesWith(simdSize);
        }
        CI.eraseFromParent();
    }
    else if (funcName.equals(SubGroupFuncsResolution::GET_SUB_GROUP_LOCAL_ID))
    {
        // Creates intrinsics that will be lowered in the CodeGen and will handle the sub_group_local_id
        IntegerType* typeInt32 = Type::getInt32Ty(Ctx);

        Function* simdLaneIdFunc = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(), GenISAIntrinsic::GenISA_simdLaneId);
        Instruction* simdLaneId16 = CallInst::Create(simdLaneIdFunc, "simdLaneId16", &CI);
        Instruction* simdLaneId = ZExtInst::CreateIntegerCast(simdLaneId16, typeInt32, false, "simdLaneId", &CI);

        CI.replaceAllUsesWith(simdLaneId);
        CI.eraseFromParent();
    }
    else if (funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE) ||
        funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_US) ||
        funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_F) ||
        funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_H) ||
        funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_C) ||
        funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_B) ||
        funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_DF)
        )
    {
        CheckSIMDSize(CI, "Shuffle not supported in SIMD32");

        // Creates intrinsics that will be lowered in the CodeGen and will handle the sub_group_shuffle function
        IRBuilder<> IRB(&CI);
        Value* args[3];
        args[0] = CI.getArgOperand(0);
        args[1] = CI.getArgOperand(1);
        args[2] = IRB.getInt32(0);

        Function* simdShuffleFunc = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_WaveShuffleIndex, args[0]->getType());
        Instruction* simdShuffle = CallInst::Create(simdShuffleFunc, args, "simdShuffle", &CI);

        CI.replaceAllUsesWith(simdShuffle);
        CI.eraseFromParent();
    }
    else if (funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_DOWN) ||
        funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_DOWN_US) ||
        funcName.equals(SubGroupFuncsResolution::SUB_GROUP_SHUFFLE_DOWN_UC))
    {
        CheckSIMDSize(CI, "Shuffle Down not supported in SIMD32");

        // Creates intrinsics that will be lowered in the CodeGen and will handle the sub_group_shuffle_down function
        Value* args[3];
        args[0] = CI.getArgOperand(0);
        args[1] = CI.getArgOperand(1);
        args[2] = CI.getArgOperand(2);

        Function* simdShuffleDownFunc = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_simdShuffleDown,
            args[0]->getType());
        Instruction* simdShuffleDown = CallInst::Create(simdShuffleDownFunc, args, "simdShuffleDown", &CI);

        CI.replaceAllUsesWith(simdShuffleDown);
        CI.eraseFromParent();
    }
    else if (funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_1_GBL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_2_GBL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_4_GBL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_8_GBL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_1_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_2_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_4_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_8_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_16_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_1_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_2_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_4_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_8_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_16_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_1_GBL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_2_GBL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_4_GBL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_8_GBL_L))
    {
        CheckSIMDSize(CI, "Block reads not supported in SIMD32");
        simdBlockRead(CI);
    }
    else if (funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_GBL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_GBL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_GBL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_GBL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_16_GBL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_16_GBL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_GBL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_GBL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_GBL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_GBL_L))
    {
        CheckSIMDSize(CI, "Block writes not supported in SIMD32");
        simdBlockWrite(CI);
    }
    else if (funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_1_LCL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_2_LCL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_4_LCL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_8_LCL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_1_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_2_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_4_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_8_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_16_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_1_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_2_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_4_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_8_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_16_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_1_LCL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_2_LCL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_4_LCL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_READ_8_LCL_L))
    {
        CheckSIMDSize(CI, "Block reads not supported in SIMD32");
        simdBlockRead(CI);
    }
    else if (funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_LCL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_LCL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_LCL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_LCL) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_16_LCL_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_16_LCL_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_1_LCL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_2_LCL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_4_LCL_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_BLOCK_WRITE_8_LCL_L))
    {
        CheckSIMDSize(CI, "Block writes not supported in SIMD32");
        simdBlockWrite(CI);
    }
    else if (funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_1) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_2) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_4) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_8) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_1_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_2_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_4_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_8_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_16_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_1_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_2_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_4_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_8_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_16_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_1_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_2_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_4_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_READ_8_L))
    {
        CheckSIMDSize(CI, "SIMD Media Block Read not supported in SIMD32");
        mediaBlockRead(CI);
    }
    else if (funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_1) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_2) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_4) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_8) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_1_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_2_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_4_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_8_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_16_B) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_1_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_2_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_4_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_8_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_16_H) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_1_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_2_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_4_L) ||
        funcName.equals(SubGroupFuncsResolution::SIMD_MEDIA_BLOCK_WRITE_8_L))
    {
        CheckSIMDSize(CI, "SIMD Media Block Write not supported in SIMD32");
        mediaBlockWrite(CI);
    }
    else if (funcName.startswith(SubGroupFuncsResolution::MEDIA_BLOCK_READ))
    {
        // Creates intrinsics that will be lowered in the CodeGen and will handle the media_block_read

        SmallVector<Value*, 5> args;
        pushMediaBlockArgs(args, CI);

        // The spec requires that the width and height are compile-time constants.
        if (!isa<ConstantInt>(CI.getOperand(2)))
        {
            m_pCtx->EmitError("width argument supplied to intel_media_block_read*() must be constant.");
            return;
        }

        if (!isa<ConstantInt>(CI.getOperand(3)))
        {
            m_pCtx->EmitError("height argument supplied to intel_media_block_read*() must be constant.");
            return;
        }

        args.push_back(CI.getArgOperand(2)); // blockWidth
        args.push_back(CI.getArgOperand(3)); // blockHeight

        Function* MediaBlockReadFunc = GenISAIntrinsic::getDeclaration(
            CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_MediaBlockRead,
            CI.getCalledFunction()->getReturnType());

        auto* MediaBlockRead = cast<GenIntrinsicInst>(
            CallInst::Create(MediaBlockReadFunc, args, "", &CI));
        MediaBlockRead->setDebugLoc(CI.getDebugLoc());

        CheckMediaBlockInstError(MediaBlockRead, true);
        //Return if any error
        if (!m_pCtx->oclErrorMessage.empty())
        {
            return;
        }

        CI.replaceAllUsesWith(MediaBlockRead);
        CI.eraseFromParent();
    }
    else if (funcName.startswith(SubGroupFuncsResolution::MEDIA_BLOCK_WRITE))
    {
        // Creates intrinsics that will be lowered in the CodeGen and will handle the media_block_write

        SmallVector<Value*, 5> args;
        pushMediaBlockArgs(args, CI);

        // The spec requires that the width and height are compile-time constants.
        if (!isa<ConstantInt>(CI.getOperand(2)))
        {
            m_pCtx->EmitError("width argument supplied to intel_media_block_write*() must be constant.");
            return;
        }

        if (!isa<ConstantInt>(CI.getOperand(3)))
        {
            m_pCtx->EmitError("height argument supplied to intel_media_block_write*() must be constant.");
            return;
        }

        args.push_back(CI.getArgOperand(2)); // blockWidth
        args.push_back(CI.getArgOperand(3)); // blockHeight
        args.push_back(CI.getArgOperand(4)); // pixels

        Function* MediaBlockWriteFunc = GenISAIntrinsic::getDeclaration(
            CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_MediaBlockWrite,
            CI.getArgOperand(4)->getType());

        auto* MediaBlockWrite = cast<GenIntrinsicInst>(
            CallInst::Create(MediaBlockWriteFunc, args, "", &CI));
        MediaBlockWrite->setDebugLoc(CI.getDebugLoc());

        CheckMediaBlockInstError(MediaBlockWrite, false);
        //Return if any error
        if (!m_pCtx->oclErrorMessage.empty())
        {
            return;
        }

        CI.replaceAllUsesWith(MediaBlockWrite);
        CI.eraseFromParent();
    }
    else if (funcName.equals(SubGroupFuncsResolution::MEDIA_BLOCK_RECTANGLE_READ))
    {
        // Creates intrinsics that will be lowered in the CodeGen and will handle the simd_media_block_read_8
        SmallVector<Value*, 5> args;
        pushMediaBlockArgs(args, CI);

        args.push_back(CI.getArgOperand(2)); // blockWidth
        args.push_back(CI.getArgOperand(3)); // blockHeight
        args.push_back(CI.getArgOperand(4)); // destination

        Function* MediaBlockRectangleReadFunc = GenISAIntrinsic::getDeclaration(CI.getCalledFunction()->getParent(), GenISAIntrinsic::GenISA_MediaBlockRectangleRead);
        Instruction* MediaBlockRectangleRead = CallInst::Create(MediaBlockRectangleReadFunc, args, "", &CI);

        CI.replaceAllUsesWith(MediaBlockRectangleRead);
        CI.eraseFromParent();
    }
    else if (funcName.equals(SubGroupFuncsResolution::GET_IMAGE_BTI))
    {
        if (m_argIndexMap.empty())
        {
            BTIHelper(CI);
        }

        Argument* pImg = nullptr;
        ConstantInt* imageIndex = IGC::CImagesBI::CImagesUtils::getImageIndex(&m_argIndexMap, &CI, 0, pImg);

        CI.replaceAllUsesWith(imageIndex);
        CI.eraseFromParent();
    }
    else if (funcName.startswith(SubGroupFuncsResolution::SUB_GROUP_REDUCE))
    {
        return subGroupArithmetic( CI, GetWaveOp(funcName), GroupOperationReduce);
    }
    else if (funcName.startswith(SubGroupFuncsResolution::SUB_GROUP_SCAN))
    {
        return subGroupArithmetic( CI, GetWaveOp(funcName), GroupOperationScan);
    }
    else if (funcName.startswith(SubGroupFuncsResolution::SUB_GROUP_CLUSTERED_REDUCE))
    {
        return subGroupArithmetic( CI, GetWaveOp(funcName), GroupOperationClusteredReduce);
    }
    else if (funcName.startswith(SubGroupFuncsResolution::SUB_GROUP_BARRIER))
    {
        Function* waveBarrier = GenISAIntrinsic::getDeclaration(
            CI.getCalledFunction()->getParent(),
            GenISAIntrinsic::GenISA_wavebarrier);
        CallInst::Create(waveBarrier, "", &CI);
        CI.eraseFromParent();
    }
    else
    {
        // Non Sub Group function, do nothing
        return;
    }
    m_changed = true;
}

void SubGroupFuncsResolution::CheckMediaBlockInstError(llvm::GenIntrinsicInst* inst, bool isRead)
{
    Function* F = inst->getParent()->getParent();

    //Width and height must be supplied as compile time constants.
    uint blockWidth = (uint)cast<ConstantInt>(inst->getOperand(4))->getZExtValue();
    uint blockHeight = (uint)cast<ConstantInt>(inst->getOperand(5))->getZExtValue();

    //Code to extract subgroup size
    IGC::IGCMD::MetaDataUtils* pMdUtils = getAnalysis<MetaDataUtilsWrapper>().getMetaDataUtils();
    IGC::IGCMD::FunctionInfoMetaDataHandle funcInfoMD = pMdUtils->getFunctionsInfoItem(F);
    unsigned int subGrpSize = funcInfoMD->getSubGroupSize()->getSIMD_size();

    auto* pFunc = inst->getCalledFunction();
    auto* pDataType = isRead ? pFunc->getReturnType() : inst->getOperand(6)->getType();

    const llvm::DataLayout* DL = &F->getParent()->getDataLayout();

    uint typeSize = isa<VectorType>(pDataType) ?
        (uint)DL->getTypeSizeInBits(cast<VectorType>(pDataType)->getElementType()) / 8 :
        (uint)DL->getTypeSizeInBits(pDataType) / 8;

    uint widthInBytes = blockWidth * typeSize;
    uint IOSize = widthInBytes * blockHeight;

    // Determine max rows that can be read by hardware for the given width.
    uint maxRows = 0;
    if (widthInBytes <= 4)
    {
        maxRows = 64;
    }
    else if (widthInBytes <= 8)
    {
        maxRows = 32;
    }
    else if (widthInBytes <= 16)
    {
        maxRows = 16;
    }
    else
    {
        maxRows = 8;
    }

    {
        std::string builtinPrefix = isRead ? "intel_media_block_read" : "intel_media_block_write";

        if (widthInBytes > 32) // hardware restriction on block read width
        {
            std::string output;
            raw_string_ostream S(output);
            S << "width for " << builtinPrefix << "*() must be <= " << 32 / typeSize;
            S.flush();
            m_pCtx->EmitError(output.c_str());
            return;
        }

        if (blockHeight > maxRows) // hardware restriction on block read height
        {
            std::string output;
            raw_string_ostream S(output);
            S << "height for " << widthInBytes << " bytes wide "
                << builtinPrefix << "*() must be <= " << maxRows;
            S.flush();
            m_pCtx->EmitError(output.c_str());
            return;
        }

        if (subGrpSize != 0)
        {
            uint maxIOSize = subGrpSize * ((uint)DL->getTypeSizeInBits(pDataType) / 8);

            if (IOSize > maxIOSize)
            {
                std::string output;
                raw_string_ostream S(output);
                S << builtinPrefix << "*() attempt of " << IOSize <<
                    " bytes.  Must be <= " << maxIOSize << " bytes.";
                S.flush();
                m_pCtx->EmitError(output.c_str());
                return;
            }
        }

        if (widthInBytes % 4 != 0)
        {
            std::string output;
            raw_string_ostream S(output);
            if (typeSize == 1)
            {
                S << builtinPrefix << "_uc*() widths must be quad pixel aligned.";
            }
            else
            {
                S << builtinPrefix << "_us*() widths must be dual pixel aligned.";
            }
            S.flush();
            m_pCtx->EmitError(output.c_str());
            return;
        }
    }
}
