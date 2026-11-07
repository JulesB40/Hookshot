// GPL-3.0-or-later. Runtime address inputs exercise the production guest page-index emitter.
#include <cstdint>
#include <cstring>
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/backend/spirv/guest_page_lookup.h"
#include "shader_recompiler/frontend/ir/program.h"
#if defined(_WIN32)
#define HK_LAYOUT_EXPORT __declspec(dllexport)
#else
#define HK_LAYOUT_EXPORT __attribute__((visibility("default")))
#endif
extern "C" HK_LAYOUT_EXPORT size_t hookshot_vk_layout_test_spirv(uint32_t* output,size_t capacity,uint32_t reserved) {
    (void)reserved;
    using namespace Shader;
    using namespace Shader::Backend::SPIRV;
    try {
        Profile profile{}; profile.supported_spirv=0x00010300;
        profile.support_int64=true; profile.unified_descriptor_binding=true;
        IR::Program program{}; program.stage=Stage::Compute; program.workgroup_size={64,1,1};
        program.info.uses_local_invocation_id=true; program.info.uses_int64=true;
        RuntimeInfo runtime{}; Shader::Backend::Bindings bindings{};
        EmitContext ctx(profile,runtime,program,bindings);
        ctx.AddCapability(spv::Capability::Int64);
        ctx.SetMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
        const Id array=ctx.TypeRuntimeArray(ctx.U32[1]); ctx.Decorate(array,spv::Decoration::ArrayStride,4u);
        const Id block=ctx.TypeStruct(array); ctx.Decorate(block,spv::Decoration::Block);
        ctx.MemberDecorate(block,0,spv::Decoration::Offset,0u);
        const Id storage=ctx.AddGlobalVariable(ctx.TypePointer(spv::StorageClass::StorageBuffer,block),spv::StorageClass::StorageBuffer);
        ctx.Decorate(storage,spv::Decoration::DescriptorSet,0u); ctx.Decorate(storage,spv::Decoration::Binding,0u);
        const Id fn=ctx.OpFunction(ctx.TypeVoid(),spv::FunctionControlMask::MaskNone,ctx.TypeFunction(ctx.TypeVoid()));
        ctx.AddLabel();
        const Id index=ctx.OpCompositeExtract(ctx.U32[1],ctx.OpLoad(ctx.U32[3],ctx.local_invocation_id),0u);
        const Id base=ctx.OpIMul(ctx.U32[1],index,ctx.Const(4u));
        const Id ptr=ctx.TypePointer(spv::StorageClass::StorageBuffer,ctx.U32[1]);
        const auto at=[&](uint32_t col) { return ctx.OpAccessChain(ptr,storage,ctx.Const(0u),ctx.OpIAdd(ctx.U32[1],base,ctx.Const(col))); };
        const Id low=ctx.OpLoad(ctx.U32[1],at(0)), high=ctx.OpLoad(ctx.U32[1],at(1));
        const Id addr=ctx.OpBitcast(ctx.U64,ctx.OpCompositeConstruct(ctx.U32[2],low,high));
        const auto lookup=EmitGuestPageLookup(ctx,addr);
        ctx.OpStore(at(2),ctx.OpSelect(ctx.U32[1],lookup.inside,ctx.Const(1u),ctx.Const(0u)));
        ctx.OpStore(at(3),ctx.OpSelect(ctx.U32[1],lookup.inside,ctx.OpUConvert(ctx.U32[1],lookup.index),ctx.Const(0xffffffffu)));
        ctx.OpReturn(); ctx.OpFunctionEnd();
        ctx.AddEntryPoint(spv::ExecutionModel::GLCompute,fn,"main",ctx.interfaces);
        ctx.AddExecutionMode(fn,spv::ExecutionMode::LocalSize,64u,1u,1u);
        const auto code=ctx.Assemble();
        if (output && capacity>=code.size()) std::memcpy(output,code.data(),code.size()*4);
        return code.size();
    } catch (...) { return 0; }
}
