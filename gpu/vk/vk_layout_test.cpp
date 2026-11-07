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
    const bool bridge=reserved!=0;
    using namespace Shader;
    using namespace Shader::Backend::SPIRV;
    try {
        Profile profile{}; profile.supported_spirv=0x00010300;
        profile.support_int64=bridge; profile.unified_descriptor_binding=true;
        IR::Program program{}; program.stage=Stage::Compute; program.workgroup_size={64,1,1};
        program.info.uses_local_invocation_id=true; program.info.uses_int64=bridge;
        RuntimeInfo runtime{}; Shader::Backend::Bindings bindings{};
        EmitContext ctx(profile,runtime,program,bindings);
        if (bridge) ctx.AddCapability(spv::Capability::Int64);
        ctx.SetMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
        const Id array=ctx.TypeRuntimeArray(ctx.U32[1]); ctx.Decorate(array,spv::Decoration::ArrayStride,4u);
        const Id block=ctx.TypeStruct(array); ctx.Decorate(block,spv::Decoration::Block);
        ctx.MemberDecorate(block,0,spv::Decoration::Offset,0u);
        const Id storage=ctx.AddGlobalVariable(ctx.TypePointer(spv::StorageClass::StorageBuffer,block),spv::StorageClass::StorageBuffer);
        ctx.Decorate(storage,spv::Decoration::DescriptorSet,0u); ctx.Decorate(storage,spv::Decoration::Binding,0u);
        const Id fn=ctx.OpFunction(ctx.TypeVoid(),spv::FunctionControlMask::MaskNone,ctx.TypeFunction(ctx.TypeVoid()));
        ctx.AddLabel();
        const Id index=ctx.OpCompositeExtract(ctx.U32[1],ctx.OpLoad(ctx.U32[3],ctx.local_invocation_id),0u);
        const Id base=ctx.OpIMul(ctx.U32[1],index,ctx.Const(16u));
        const Id ptr=ctx.TypePointer(spv::StorageClass::StorageBuffer,ctx.U32[1]);
        const auto at=[&](uint32_t col) { return ctx.OpAccessChain(ptr,storage,ctx.Const(0u),ctx.OpIAdd(ctx.U32[1],base,ctx.Const(col))); };
        const Id low=ctx.OpLoad(ctx.U32[1],at(0)), high=ctx.OpLoad(ctx.U32[1],at(1));
        const auto direct=EmitGuestPageLookup32(ctx,low,high);
        Id round_low=low,round_high=high,bridge_index=ctx.Const(0xf00d0000u),wide_high=ctx.Const(0u);
        if (bridge) {
            const Id addr=ctx.OpBitcast(ctx.U64,ctx.OpCompositeConstruct(ctx.U32[2],low,high));
            const Id round=ctx.OpBitcast(ctx.U32[2],addr);
            round_low=ctx.OpCompositeExtract(ctx.U32[1],round,0u);
            round_high=ctx.OpCompositeExtract(ctx.U32[1],round,1u);
            const auto wide=EmitGuestPageLookup(ctx,addr);
            bridge_index=ctx.OpSelect(ctx.U32[1],wide.inside,ctx.OpUConvert(ctx.U32[1],wide.index),ctx.Const(0xffffffffu));
            wide_high=ctx.OpCompositeExtract(ctx.U32[1],ctx.OpBitcast(ctx.U32[2],wide.index),1u);
        }
        const Id delta=ctx.OpISub(ctx.U32[1],high,ctx.Const(static_cast<u32>(HK_GUEST_PAGES_BASE>>32)));
        const Id trace[]={ctx.OpSelect(ctx.U32[1],direct.inside,ctx.Const(1u),ctx.Const(0u)),
            ctx.OpSelect(ctx.U32[1],direct.inside,direct.index,ctx.Const(0xffffffffu)),bridge_index,
            round_low,round_high,delta,ctx.OpShiftLeftLogical(ctx.U32[1],delta,ctx.Const(20u)),
            ctx.OpShiftRightLogical(ctx.U32[1],low,ctx.Const(12u)),
            ctx.OpSelect(ctx.U32[1],direct.inside,ctx.Const(0x12345678u),ctx.Const(0xabcdef01u)),
            ctx.Const(0xffffffffu),ctx.Const(0x12345678u),index,wide_high,ctx.Const(0x75000006u)};
        for (uint32_t i=0;i<14;++i) ctx.OpStore(at(i+2),trace[i]);
        ctx.OpReturn(); ctx.OpFunctionEnd();
        ctx.AddEntryPoint(spv::ExecutionModel::GLCompute,fn,"main",ctx.interfaces);
        ctx.AddExecutionMode(fn,spv::ExecutionMode::LocalSize,64u,1u,1u);
        const auto code=ctx.Assemble();
        if (output && capacity>=code.size()) std::memcpy(output,code.data(),code.size()*4);
        return code.size();
    } catch (...) { return 0; }
}
