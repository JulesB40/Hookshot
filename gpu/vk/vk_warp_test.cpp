// GPL-3.0-or-later. Authored compute shader using the real Eden warp emitters.
// No game program, keys or guest callbacks. Output: 128 rows of twelve uint32 words.
#include <cstdint>
#include <cstring>
#include <vector>
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/runtime_info.h"
#if defined(_WIN32)
#define HK_TEST_EXPORT __declspec(dllexport)
#else
#define HK_TEST_EXPORT __attribute__((visibility("default")))
#endif
extern "C" HK_TEST_EXPORT size_t hookshot_vk_warp_test_spirv(uint32_t* output,size_t capacity,uint32_t max_lanes) {
    using namespace Shader;
    using namespace Shader::Backend::SPIRV;
    using Sirit::Id;
    if (max_lanes!=32 && max_lanes!=64 && max_lanes!=128) return 0;
    try {
        Profile profile{};
        profile.supported_spirv=0x00010300;
        profile.unified_descriptor_binding=true;
        profile.support_vote=true;
        profile.support_shuffle_relative=false;
        profile.warp_size_potentially_larger_than_guest=true;
        profile.max_subgroup_size=max_lanes;
        profile.supported_subgroup_stages=1u<<static_cast<unsigned>(Stage::Compute);
        IR::Program program{};
        program.stage=Stage::Compute; program.workgroup_size={128,1,1};
        program.info.uses_subgroup_invocation_id=true;
        program.info.uses_subgroup_vote=true; program.info.uses_subgroup_shuffles=true;
        program.info.uses_subgroup_mask=true; program.info.uses_local_invocation_id=true;
        RuntimeInfo runtime{}; Shader::Backend::Bindings bindings{};
        EmitContext ctx(profile,runtime,program,bindings);
        ctx.AddCapability(spv::Capability::GroupNonUniform);
        ctx.AddCapability(spv::Capability::GroupNonUniformBallot);
        ctx.AddCapability(spv::Capability::GroupNonUniformShuffle);
        ctx.SetMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
        const Id array=ctx.TypeRuntimeArray(ctx.U32[1]);
        ctx.Decorate(array,spv::Decoration::ArrayStride,4u);
        const Id block=ctx.TypeStruct(array);
        ctx.Decorate(block,spv::Decoration::Block);
        ctx.MemberDecorate(block,0,spv::Decoration::Offset,0u);
        const Id storage=ctx.AddGlobalVariable(ctx.TypePointer(spv::StorageClass::StorageBuffer,block),spv::StorageClass::StorageBuffer);
        ctx.Decorate(storage,spv::Decoration::DescriptorSet,0u); ctx.Decorate(storage,spv::Decoration::Binding,0u);
        const Id void_type=ctx.TypeVoid();
        const Id fn=ctx.OpFunction(void_type,spv::FunctionControlMask::MaskNone,ctx.TypeFunction(void_type));
        ctx.AddLabel();
        const Id native=ctx.OpLoad(ctx.U32[1],ctx.subgroup_local_invocation_id);
        const Id lane=EmitLaneId(ctx);
        const Id pred=ctx.OpLogicalAnd(ctx.U1,
            ctx.OpIEqual(ctx.U1,ctx.OpBitwiseAnd(ctx.U32[1],native,ctx.Const(32u)),ctx.Const(0u)),
            ctx.OpULessThan(ctx.U1,lane,ctx.Const(16u)));
        const Id value=ctx.OpIAdd(ctx.U32[1],native,ctx.Const(1000u));
        IR::Inst shuffle(IR::Opcode::ShuffleIndex,0);
        shuffle.SetArg(0,IR::Value{0u}); shuffle.SetArg(1,IR::Value{7u});
        shuffle.SetArg(2,IR::Value{31u}); shuffle.SetArg(3,IR::Value{0u});
        IR::Inst down(IR::Opcode::ShuffleDown,0);
        const Id words[]={native,lane,EmitSubgroupBallot(ctx,pred),
            ctx.OpSelect(ctx.U32[1],EmitVoteAll(ctx,pred),ctx.Const(1u),ctx.Const(0u)),
            ctx.OpSelect(ctx.U32[1],EmitVoteAny(ctx,pred),ctx.Const(1u),ctx.Const(0u)),
            ctx.OpSelect(ctx.U32[1],EmitVoteEqual(ctx,pred),ctx.Const(1u),ctx.Const(0u)),
            EmitSubgroupEqMask(ctx),EmitShuffleIndex(ctx,&shuffle,value,ctx.Const(7u),ctx.Const(31u),ctx.Const(0u)),
            EmitSubgroupBallot(ctx,ctx.true_value),EmitShuffleDown(ctx,&down,value,ctx.Const(1u),ctx.Const(31u),ctx.Const(0u)),
            ctx.OpSelect(ctx.U32[1],EmitVoteAll(ctx,ctx.true_value),ctx.Const(1u),ctx.Const(0u)),
            ctx.OpSelect(ctx.U32[1],EmitVoteAny(ctx,ctx.false_value),ctx.Const(1u),ctx.Const(0u))};
        const Id index=ctx.OpCompositeExtract(ctx.U32[1],ctx.OpLoad(ctx.U32[3],ctx.local_invocation_id),0u);
        const Id base=ctx.OpIMul(ctx.U32[1],index,ctx.Const(12u));
        const Id ptr=ctx.TypePointer(spv::StorageClass::StorageBuffer,ctx.U32[1]);
        for (uint32_t i=0;i<12;++i)
            ctx.OpStore(ctx.OpAccessChain(ptr,storage,ctx.Const(0u),ctx.OpIAdd(ctx.U32[1],base,ctx.Const(i))),words[i]);
        ctx.OpReturn(); ctx.OpFunctionEnd();
        ctx.AddEntryPoint(spv::ExecutionModel::GLCompute,fn,"main",ctx.interfaces);
        ctx.AddExecutionMode(fn,spv::ExecutionMode::LocalSize,128u,1u,1u);
        const auto code=ctx.Assemble();
        if (output && capacity>=code.size()) std::memcpy(output,code.data(),code.size()*sizeof(uint32_t));
        return code.size();
    } catch (...) { return 0; }
}
