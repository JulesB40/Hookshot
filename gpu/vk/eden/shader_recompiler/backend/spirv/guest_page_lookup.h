// GPL-3.0-or-later. Shared by production pointer following and the synthetic GPU test.
#pragma once
#include "../../../../../../shared/guest_layout.h"
namespace Shader::Backend::SPIRV {
struct GuestPageLookup { Id inside, index; };
inline GuestPageLookup EmitGuestPageLookup(EmitContext& ctx,Id addr) {
    // The Adreno 750 v0.4 run returned addr>>12 instead of (addr-base)>>12
    // for the valid 64-bit subtraction/shift chain. Keep page arithmetic in
    // 32-bit limbs, and widen only the final table index. Device retest required.
    static_assert((HK_GUEST_PAGES_BASE&0xffffffffULL)==0 && (HK_GUEST_PAGES_BYTES&0xffffffffULL)==0);
    static_assert((HK_GUEST_PAGES_BASE2&0xffffffffULL)==0 && (HK_GUEST_PAGES_BYTES2&0xffffffffULL)==0);
    static_assert(HK_GUEST_PAGE_COUNT<=0xffffffffULL);
    const Id words=ctx.OpBitcast(ctx.U32[2],addr);
    const Id lo=ctx.OpCompositeExtract(ctx.U32[1],words,0U);
    const Id hi=ctx.OpCompositeExtract(ctx.U32[1],words,1U);
    const auto in_window=[&](uint64_t base,uint64_t bytes) {
        return ctx.OpLogicalAnd(ctx.U1,
            ctx.OpUGreaterThanEqual(ctx.U1,hi,ctx.Const(static_cast<u32>(base>>32))),
            ctx.OpULessThan(ctx.U1,hi,ctx.Const(static_cast<u32>((base+bytes)>>32))));
    };
    const Id in_a=in_window(HK_GUEST_PAGES_BASE,HK_GUEST_PAGES_BYTES);
    const Id in_b=in_window(HK_GUEST_PAGES_BASE2,HK_GUEST_PAGES_BYTES2);
    const Id page_lo=ctx.OpShiftRightLogical(ctx.U32[1],lo,ctx.Const(12U));
    const auto relative_page=[&](uint64_t base) {
        const Id delta=ctx.OpISub(ctx.U32[1],hi,ctx.Const(static_cast<u32>(base>>32)));
        return ctx.OpBitwiseOr(ctx.U32[1],ctx.OpShiftLeftLogical(ctx.U32[1],delta,ctx.Const(20U)),page_lo);
    };
    const Id index32=ctx.OpSelect(ctx.U32[1],in_a,relative_page(HK_GUEST_PAGES_BASE),
        ctx.OpIAdd(ctx.U32[1],relative_page(HK_GUEST_PAGES_BASE2),ctx.Const(static_cast<u32>(HK_GUEST_PAGES_BYTES>>12))));
    return {ctx.OpLogicalOr(ctx.U1,in_a,in_b),ctx.OpUConvert(ctx.U64,index32)};
}
}
