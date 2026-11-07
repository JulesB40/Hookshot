// GPL-3.0-or-later. Exercise the actual Eden Int64 lowering pass and full SPIR-V backend.
#include <cstdint>
#include <cstring>
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "../../shared/guest_layout.h"
#if defined(_WIN32)
#define HK_LOWER_EXPORT __declspec(dllexport)
#else
#define HK_LOWER_EXPORT __attribute__((visibility("default")))
#endif
extern "C" HK_LOWER_EXPORT size_t hookshot_vk_lowered_test_spirv(uint32_t* output,size_t capacity,uint32_t) {
    using namespace Shader;
    try {
        ObjectPool<IR::Inst> pool;
        IR::Block block(pool); IR::Program program{};
        program.stage=Stage::Compute;program.workgroup_size={64,1,1};
        program.blocks.push_back(&block);program.post_order_blocks.push_back(&block);
        program.syntax_list.push_back({.data={.block=&block},.type=IR::AbstractSyntaxNode::Type::Block});
        program.syntax_list.push_back({.data={},.type=IR::AbstractSyntaxNode::Type::Return});
        program.info.uses_local_invocation_id=true;
        program.info.storage_buffers_descriptors.push_back({0,0,1,true});
        program.info.nvn_buffer_used.set(0);program.info.used_storage_buffer_types=IR::Type::U32;
        IR::IREmitter ir(block); ir.Prologue();
        const auto row=ir.LocalInvocationIdX();
        const IR::U32 base{ir.IMul(row,ir.Imm32(64u))};
        const auto append=[&](IR::Opcode op,std::initializer_list<IR::Value> args) {
            block.AppendNewInst(op,args);return IR::Value{&block.Instructions().back()};
        };
        const IR::U32 lo{append(IR::Opcode::LoadStorage32,{ir.Imm32(0u),base})};
        const IR::U32 hi{append(IR::Opcode::LoadStorage32,{ir.Imm32(0u),ir.IAdd(base,ir.Imm32(4u))})};
        const auto address=ir.PackUint2x32(ir.CompositeConstruct(lo,hi));
        const IR::U64 one=ir.Imm64(u64{1});
        const IR::U64 values[]={IR::U64{ir.IAdd(address,one)},IR::U64{ir.ISub(address,one)},
            IR::U64{ir.ShiftLeftLogical(address,row)},IR::U64{ir.ShiftRightLogical(address,row)},
            IR::U64{ir.INeg(address)},ir.PackUint2x32(ir.CompositeConstruct(
                ir.BitwiseXor(lo,ir.Imm32(0x12345678u)),ir.BitwiseXor(hi,ir.Imm32(0xa55a5aa5u))))};
        const auto store=[&](u32 col,const IR::U32& value) {
            append(IR::Opcode::WriteStorage32,{ir.Imm32(0u),ir.IAdd(base,ir.Imm32(col*4)),value});
        };
        for (u32 i=0;i<6;++i) {
            const auto words=ir.UnpackUint2x32(values[i]);
            store(2+i*2,IR::U32{ir.CompositeExtract(words,0u)});
            store(3+i*2,IR::U32{ir.CompositeExtract(words,1u)});
        }
        const auto greater=ir.IGreaterThan(hi,ir.Imm32(static_cast<u32>(HK_GUEST_HEAP_BASE>>32)),false);
        const auto equal=ir.IEqual(hi,ir.Imm32(static_cast<u32>(HK_GUEST_HEAP_BASE>>32)));
        const auto ge_low=ir.IGreaterThanEqual(lo,ir.Imm32(0u),false);
        store(14,IR::U32{ir.Select(ir.LogicalOr(greater,ir.LogicalAnd(equal,ge_low)),ir.Imm32(1u),ir.Imm32(0u))});
        store(15,ir.Imm32(0x75000009u)); ir.Epilogue();
        Optimization::LowerInt64ToInt32(program);
        Optimization::IdentityRemovalPass(program);
        Optimization::DeadCodeEliminationPass(program);
        Profile profile{};profile.supported_spirv=0x00010300;profile.unified_descriptor_binding=true;
        profile.support_int64=false;profile.support_descriptor_aliasing=false;
        program.info.uses_int64=false;
        RuntimeInfo runtime{};Backend::Bindings bindings{};
        const auto code=Backend::SPIRV::EmitSPIRV(profile,runtime,program,bindings);
        if (output && capacity>=code.size()) std::memcpy(output,code.data(),code.size()*4);
        return code.size();
    } catch (...) { return 0; }
}
