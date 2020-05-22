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

#include "AdaptorCommon/ImplicitArgs.hpp"
#include "Compiler/Optimizer/OpenCLPasses/ProgramScopeConstants/ProgramScopeConstantAnalysis.hpp"
#include "Compiler/IGCPassSupport.h"
#include "common/LLVMWarningsPush.hpp"
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/ValueTracking.h>
#include "common/LLVMWarningsPop.hpp"
#include "Probe/Assertion.h"

using namespace llvm;
using namespace IGC;
using namespace IGC::IGCMD;

// Register pass to igc-opt
#define PASS_FLAG "igc-programscope-constant-analysis"
#define PASS_DESCRIPTION "Creates annotations for OpenCL program-scope structures"
#define PASS_CFG_ONLY false
#define PASS_ANALYSIS false
IGC_INITIALIZE_PASS_BEGIN(ProgramScopeConstantAnalysis, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)
IGC_INITIALIZE_PASS_DEPENDENCY(MetaDataUtilsWrapper)
IGC_INITIALIZE_PASS_END(ProgramScopeConstantAnalysis, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)

char ProgramScopeConstantAnalysis::ID = 0;

ProgramScopeConstantAnalysis::ProgramScopeConstantAnalysis() : ModulePass(ID)
{
    initializeProgramScopeConstantAnalysisPass(*PassRegistry::getPassRegistry());
}

bool ProgramScopeConstantAnalysis::runOnModule(Module& M)
{
    bool hasInlineConstantBuffer = false;
    bool hasInlineGlobalBuffer = false;

    BufferOffsetMap inlineProgramScopeOffsets;

    // maintains pointer information so we can patch in
    // actual pointer addresses in runtime.
    PointerOffsetInfoList pointerOffsetInfoList;

    LLVMContext& C = M.getContext();
    m_DL = &M.getDataLayout();

    MetaDataUtils* mdUtils = getAnalysis<MetaDataUtilsWrapper>().getMetaDataUtils();
    ModuleMetaData* modMd = getAnalysis<MetaDataUtilsWrapper>().getModuleMetaData();

    SmallVector<GlobalVariable*, 32> zeroInitializedGlobals;

    for (Module::global_iterator I = M.global_begin(), E = M.global_end(); I != E; ++I)
    {
        GlobalVariable* globalVar = &(*I);

        PointerType* ptrType = cast<PointerType>(globalVar->getType());
        IGC_ASSERT(ptrType && "The type of a global variable must be a pointer type");

        // Pointer's address space should be either constant or global
        // The ?: is a workaround for clang bug, clang creates string constants with private address sapce!
        // When clang bug is fixed it should become:
        // const unsigned AS = ptrType->getAddressSpace();
        const unsigned AS = ptrType->getAddressSpace() != ADDRESS_SPACE_PRIVATE ? ptrType->getAddressSpace() : ADDRESS_SPACE_CONSTANT;

        // local address space variables are also generated as GlobalVariables.
        // Ignore them here.
        if (AS == ADDRESS_SPACE_LOCAL)
        {
            continue;
        }

        if (AS != ADDRESS_SPACE_CONSTANT &&
            AS != ADDRESS_SPACE_GLOBAL)
        {
            IGC_ASSERT(false && "program scope variable with unexpected address space");
            continue;
        }

        // The only way to get a null initializer is via an external variable.
        // Linking has already occurred; everything should be resolved.
        Constant* initializer = globalVar->getInitializer();
        IGC_ASSERT(initializer && "Constant must be initialized");
        if (!initializer)
        {
            continue;
        }

        // If this variable isn't used, don't add it to the buffer.
        if (globalVar->use_empty())
        {
            // If compiler requests global symbol for external/common linkage, add it reguardless if it is used
            bool requireGlobalSymbol = modMd->compOpt.EnableTakeGlobalAddress &&
                (globalVar->hasCommonLinkage() || globalVar->hasExternalLinkage());

            if (!requireGlobalSymbol)
                continue;
        }

        DataVector* inlineProgramScopeBuffer = nullptr;
        if (AS == ADDRESS_SPACE_GLOBAL)
        {
            if (!hasInlineGlobalBuffer)
            {
                InlineProgramScopeBuffer ilpsb;
                ilpsb.alignment = 0;
                ilpsb.allocSize = 0;
                modMd->inlineGlobalBuffers.push_back(ilpsb);
                hasInlineGlobalBuffer = true;
            }
            inlineProgramScopeBuffer = &modMd->inlineGlobalBuffers.back().Buffer;
        }
        else
        {
            if (!hasInlineConstantBuffer)
            {
                InlineProgramScopeBuffer ilpsb;
                ilpsb.alignment = 0;
                ilpsb.allocSize = 0;
                modMd->inlineConstantBuffers.push_back(ilpsb);
                hasInlineConstantBuffer = true;
            }
            inlineProgramScopeBuffer = &modMd->inlineConstantBuffers.back().Buffer;
        }

        // For zero initialized values, we dont need to copy the data, just tell driver how much to allocate
        if (initializer->isZeroValue())
        {
            zeroInitializedGlobals.push_back(globalVar);
            continue;
        }

        // Align the buffer.
        if (inlineProgramScopeBuffer->size() != 0)
        {
            alignBuffer(*inlineProgramScopeBuffer, m_DL->getPreferredAlignment(globalVar));
        }

        // Ok, buffer is aligned, remember where this inline variable starts.
        inlineProgramScopeOffsets[globalVar] = inlineProgramScopeBuffer->size();

        // Add the data to the buffer
        addData(initializer, *inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, AS);
    }

    // Set the needed allocation size to the actual buffer size
    if (hasInlineGlobalBuffer)
        modMd->inlineGlobalBuffers.back().allocSize = modMd->inlineGlobalBuffers.back().Buffer.size();
    if (hasInlineConstantBuffer)
        modMd->inlineConstantBuffers.back().allocSize = modMd->inlineConstantBuffers.back().Buffer.size();

    // Calculate the correct offsets for zero-initialized globals/constants
    // Total allocation size in runtime needs to include zero-init values, but data copied to compiler output can ignore them
    for (auto globalVar : zeroInitializedGlobals)
    {
        unsigned AS = cast<PointerType>(globalVar->getType())->getAddressSpace();
        unsigned &offset = (AS == ADDRESS_SPACE_GLOBAL) ? modMd->inlineGlobalBuffers.back().allocSize : modMd->inlineConstantBuffers.back().allocSize;
        offset = iSTD::Align(offset, m_DL->getPreferredAlignment(globalVar));
        inlineProgramScopeOffsets[globalVar] = offset;
        offset += (unsigned)(m_DL->getTypeAllocSize(globalVar->getType()->getPointerElementType()));
    }

    if (inlineProgramScopeOffsets.size())
    {
        // Add globals tracked in metadata to the "llvm.used" list so they won't be deleted by optimizations
        llvm::SmallVector<GlobalValue*, 4> gvec;
        for (auto Node : inlineProgramScopeOffsets)
        {
            gvec.push_back(Node.first);
        }
        ArrayRef<GlobalValue*> globalArray(gvec);
        IGC::appendToUsed(M, globalArray);
    }

    if (hasInlineConstantBuffer)
    {
        // Just add the implicit argument to each function if a constant
        // buffer has been created.  This will technically burn a patch
        // token on kernels that don't actually use the buffer but it saves
        // us having to walk the def-use chain (we can't just check if a
        // constant is used in the kernel; for example, a global buffer
        // may contain pointers that in turn point into the constant
        // address space).
        for (auto& pFunc : M)
        {
            if (pFunc.isDeclaration()) continue;
            // Don't add implicit arg if doing relocation
            if (pFunc.hasFnAttribute("EnableGlobalRelocation")) continue;

            SmallVector<ImplicitArg::ArgType, 1> implicitArgs;
            implicitArgs.push_back(ImplicitArg::CONSTANT_BASE);
            ImplicitArgs::addImplicitArgs(pFunc, implicitArgs, mdUtils);
        }
        mdUtils->save(C);
    }

    if (hasInlineGlobalBuffer)
    {
        for (auto& pFunc : M)
        {
            if (pFunc.isDeclaration()) continue;
            // Don't add implicit arg if doing relocation
            if (pFunc.hasFnAttribute("EnableGlobalRelocation")) continue;

            SmallVector<ImplicitArg::ArgType, 1> implicitArgs;
            implicitArgs.push_back(ImplicitArg::GLOBAL_BASE);
            ImplicitArgs::addImplicitArgs(pFunc, implicitArgs, mdUtils);
        }
        mdUtils->save(C);
    }

    // Setup the metadata for pointer patch info to be utilized during
    // OCL codegen.

    if (pointerOffsetInfoList.size() > 0)
    {
        for (auto& info : pointerOffsetInfoList)
        {
            // We currently just use a single buffer at index 0; hardcode
            // the patch to reference it.

            if (info.AddressSpaceWherePointerResides == ADDRESS_SPACE_GLOBAL)
            {
                PointerProgramBinaryInfo ppbi;
                ppbi.PointerBufferIndex = 0;
                ppbi.PointerOffset = int_cast<int32_t>(info.PointerOffsetFromBufferBase);
                ppbi.PointeeBufferIndex = 0;
                ppbi.PointeeAddressSpace = info.AddressSpacePointedTo;
                modMd->GlobalPointerProgramBinaryInfos.push_back(ppbi);
            }
            else if (info.AddressSpaceWherePointerResides == ADDRESS_SPACE_CONSTANT)
            {
                PointerProgramBinaryInfo ppbi;
                ppbi.PointerBufferIndex = 0;
                ppbi.PointerOffset = int_cast<int32_t>(info.PointerOffsetFromBufferBase);
                ppbi.PointeeBufferIndex = 0;
                ppbi.PointeeAddressSpace = info.AddressSpacePointedTo;
                modMd->ConstantPointerProgramBinaryInfos.push_back(ppbi);
            }
            else
            {
                IGC_ASSERT(false && "trying to patch unsupported address space");
            }
        }

        mdUtils->save(C);
    }

    const bool changed = !inlineProgramScopeOffsets.empty();
    for (auto offset : inlineProgramScopeOffsets)
    {
        modMd->inlineProgramScopeOffsets[offset.first] = offset.second;
    }

    if (changed)
    {
        mdUtils->save(C);
    }

    return changed;
}

void ProgramScopeConstantAnalysis::alignBuffer(DataVector& buffer, unsigned int alignment)
{
    int bufferLen = buffer.size();
    int alignedLen = iSTD::Align(bufferLen, alignment);
    if (alignedLen > bufferLen)
    {
        buffer.insert(buffer.end(), alignedLen - bufferLen, 0);
    }
}

/////////////////////////////////////////////////////////////////
//
// WalkCastsToFindNamedAddrSpace()
//
// If a generic address space pointer is discovered, we attmept
// to walk back to find the named address space if we can.
//
static unsigned WalkCastsToFindNamedAddrSpace(const Value* val)
{
    IGC_ASSERT(isa<PointerType>(val->getType()));

    const unsigned currAddrSpace = cast<PointerType>(val->getType())->getAddressSpace();

    if (currAddrSpace != ADDRESS_SPACE_GENERIC)
    {
        return currAddrSpace;
    }

    if (const Operator * op = dyn_cast<Operator>(val))
    {
        // look through the bitcast (to be addrspacecast in 3.4).
        if (op->getOpcode() == Instruction::BitCast ||
            op->getOpcode() == Instruction::AddrSpaceCast)
        {
            return WalkCastsToFindNamedAddrSpace(op->getOperand(0));
        }
        // look through the (inttoptr (ptrtoint @a)) combo.
        else if (op->getOpcode() == Instruction::IntToPtr)
        {
            if (const Operator * opop = dyn_cast<Operator>(op->getOperand(0)))
            {
                if (opop->getOpcode() == Instruction::PtrToInt)
                {
                    return WalkCastsToFindNamedAddrSpace(opop->getOperand(0));
                }
            }
        }
        // Just look through the gep if it does no offset arithmetic.
        else if (const GEPOperator * GEP = dyn_cast<GEPOperator>(op))
        {
            if (GEP->hasAllZeroIndices())
            {
                return WalkCastsToFindNamedAddrSpace(GEP->getPointerOperand());
            }
        }
    }

    return currAddrSpace;
}

void ProgramScopeConstantAnalysis::addData(Constant* initializer,
    DataVector& inlineProgramScopeBuffer,
    PointerOffsetInfoList& pointerOffsetInfoList,
    BufferOffsetMap& inlineProgramScopeOffsets,
    unsigned addressSpace)
{
    // Initial alignment padding before insert the current constant into the buffer.
    alignBuffer(inlineProgramScopeBuffer, m_DL->getABITypeAlignment(initializer->getType()));

    // We need to do extra work with pointers here: we don't know their actual addresses
    // at compile time so we find the offset from the base of the buffer they point to
    // so we can patch in the absolute address later.
    if (PointerType * ptrType = dyn_cast<PointerType>(initializer->getType()))
    {
        int64_t offset = 0;
        const unsigned int pointerSize = int_cast<unsigned int>(m_DL->getTypeAllocSize(ptrType));
        // This case is the most common: here, we look for a pointer that can be decomposed into
        // a base + offset with the base itself being another global variable previously defined.
        if (GlobalVariable * ptrBase = dyn_cast<GlobalVariable>(GetPointerBaseWithConstantOffset(initializer, offset, *m_DL)))
        {
            const unsigned pointedToAddrSpace = WalkCastsToFindNamedAddrSpace(initializer);

            IGC_ASSERT(addressSpace == ADDRESS_SPACE_GLOBAL || addressSpace == ADDRESS_SPACE_CONSTANT);

            // We can only patch global and constant pointers.
            if ((pointedToAddrSpace == ADDRESS_SPACE_GLOBAL ||
                pointedToAddrSpace == ADDRESS_SPACE_CONSTANT) &&
                (addressSpace == ADDRESS_SPACE_GLOBAL ||
                    addressSpace == ADDRESS_SPACE_CONSTANT))
            {
                auto iter = inlineProgramScopeOffsets.find(ptrBase);
                IGC_ASSERT(iter != inlineProgramScopeOffsets.end());

                const uint64_t pointeeOffset = iter->second + offset;

                pointerOffsetInfoList.push_back(
                    PointerOffsetInfo(
                        addressSpace,
                        inlineProgramScopeBuffer.size(),
                        pointedToAddrSpace));

                // Insert just the offset of the pointer.  The base address of the buffer it points
                // to will be added to it at runtime.
                inlineProgramScopeBuffer.insert(
                    inlineProgramScopeBuffer.end(), (char*)& pointeeOffset, ((char*)& pointeeOffset) + pointerSize);
            }
            else
            {
                // Just insert zero here.  This may be some pointer to private that will be set sometime later
                // inside a kernel.  We can't patch it in so we just set it to zero here.
                inlineProgramScopeBuffer.insert(inlineProgramScopeBuffer.end(), pointerSize, 0);
            }
        }
        else if (dyn_cast<ConstantPointerNull>(initializer))
        {
            inlineProgramScopeBuffer.insert(inlineProgramScopeBuffer.end(), pointerSize, 0);
        }
        else if (isa<FunctionType>(ptrType->getElementType()))
        {
            // function pointers may be resolved anyway by the time we get to this pass?
            inlineProgramScopeBuffer.insert(inlineProgramScopeBuffer.end(), pointerSize, 0);
        }
        else if (ConstantExpr * ce = dyn_cast<ConstantExpr>(initializer))
        {
            if (ce->getOpcode() == Instruction::IntToPtr)
            {
                // intoptr can technically convert vectors of ints into vectors of pointers
                // in an LLVM sense but OpenCL has no vector of pointers type.
                if (isa<ConstantInt>(ce->getOperand(0))) {
                    uint64_t val = *cast<ConstantInt>(ce->getOperand(0))->getValue().getRawData();
                    inlineProgramScopeBuffer.insert(inlineProgramScopeBuffer.end(), (char*)& val, ((char*)& val) + pointerSize);
                }
                else {
                    addData(ce->getOperand(0), inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, addressSpace);
                }
            }
            else if (GEPOperator * GEP = dyn_cast<GEPOperator>(ce))
            {
                for (auto& Op : GEP->operands())
                    if (Constant * C = dyn_cast<Constant>(&Op))
                        addData(C, inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, addressSpace);
            }
            else if (ce->getOpcode() == Instruction::AddrSpaceCast)
            {
                if (Constant * C = dyn_cast<Constant>(ce->getOperand(0)))
                    addData(C, inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, addressSpace);
            }
            else
            {
                IGC_ASSERT(false && "unknown constant expression");
            }
        }
        else
        {
            // What other shapes can pointers take at the program scope?
            IGC_ASSERT(false && "unknown pointer shape encountered");
        }
    }
    else if (const UndefValue * UV = dyn_cast<UndefValue>(initializer))
    {
        // It's undef, just throw in zeros.
        const unsigned int zeroSize = int_cast<unsigned int>(m_DL->getTypeAllocSize(UV->getType()));
        inlineProgramScopeBuffer.insert(inlineProgramScopeBuffer.end(), zeroSize, 0);
    }
    // Must check for constant expressions before we start doing type-based checks
    else if (ConstantExpr * ce = dyn_cast<ConstantExpr>(initializer))
    {
        // Constant expressions are evil. We only handle a subset that we expect.
        // Right now, this means a bitcast, or a ptrtoint/inttoptr pair.
        // Handle it by adding the source of the cast.
        if (ce->getOpcode() == Instruction::BitCast ||
            ce->getOpcode() == Instruction::AddrSpaceCast)
        {
            addData(ce->getOperand(0), inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, addressSpace);
        }
        else if (ce->getOpcode() == Instruction::IntToPtr)
        {
            ConstantExpr* opExpr = dyn_cast<ConstantExpr>(ce->getOperand(0));
            IGC_ASSERT(opExpr && opExpr->getOpcode() == Instruction::PtrToInt && "Unexpected operand of IntToPtr");
            addData(opExpr->getOperand(0), inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, addressSpace);
        }
        else if (ce->getOpcode() == Instruction::PtrToInt)
        {
            addData(ce->getOperand(0), inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, addressSpace);
        }
        else
        {
            IGC_ASSERT(false && "Unexpected constant expression type");
        }
    }
    else if (ConstantDataSequential * cds = dyn_cast<ConstantDataSequential>(initializer))
    {
        for (unsigned i = 0; i < cds->getNumElements(); i++) {
            addData(cds->getElementAsConstant(i), inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, addressSpace);
        }
    }
    else if (ConstantAggregateZero * cag = dyn_cast<ConstantAggregateZero>(initializer))
    {
        // Zero aggregates are filled with, well, zeroes.
        const unsigned int zeroSize = int_cast<unsigned int>(m_DL->getTypeAllocSize(cag->getType()));
        inlineProgramScopeBuffer.insert(inlineProgramScopeBuffer.end(), zeroSize, 0);
    }
    // If this is an sequential type which is not a CDS or zero, have to collect the values
    // element by element. Note that this is not exclusive with the two cases above, so the
    // order of ifs is meaningful.
    else if (dyn_cast<ArrayType>(initializer->getType()) ||
             dyn_cast<StructType>(initializer->getType()) ||
             dyn_cast<VectorType>(initializer->getType()))
    {
        const int numElts = initializer->getNumOperands();
        for (int i = 0; i < numElts; ++i)
        {
            Constant* C = initializer->getAggregateElement(i);
            IGC_ASSERT(C && "getAggregateElement returned null, unsupported constant");
            // Since the type may not be primitive, extra alignment is required.
            addData(C, inlineProgramScopeBuffer, pointerOffsetInfoList, inlineProgramScopeOffsets, addressSpace);
        }
    }
    // And, finally, we have to handle base types - ints and floats.
    else
    {
        APInt intVal(32, 0, false);
        if (ConstantInt * ci = dyn_cast<ConstantInt>(initializer))
        {
            intVal = ci->getValue();
        }
        else if (ConstantFP * cfp = dyn_cast<ConstantFP>(initializer))
        {
            intVal = cfp->getValueAPF().bitcastToAPInt();
        }
        else
        {
            IGC_ASSERT(false && "Unsupported constant type");
        }

        int bitWidth = intVal.getBitWidth();
        IGC_ASSERT((bitWidth % 8 == 0) && (bitWidth <= 64) && "Unsupported bitwidth");

        const uint64_t* val = intVal.getRawData();
        inlineProgramScopeBuffer.insert(inlineProgramScopeBuffer.end(), (char*)val, ((char*)val) + (bitWidth / 8));
    }


    // final padding.  This gets used by the vec3 types that will insert zero padding at the
    // end after inserting the actual vector contents (this is due to sizeof(vec3) == 4 * sizeof(scalarType)).
    alignBuffer(inlineProgramScopeBuffer, m_DL->getABITypeAlignment(initializer->getType()));
}
