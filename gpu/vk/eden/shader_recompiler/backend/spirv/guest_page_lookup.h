// GPL-3.0-or-later. Shared by the real pointer-following path and its synthetic GPU test.
#pragma once
#include "../../../../../../shared/guest_layout.h"
namespace Shader::Backend::SPIRV {
struct GuestPageLookup { Id inside, index; };
inline GuestPageLookup EmitGuestPageLookup(EmitContext& ctx,Id addr) {
    const Id a=ctx.OpISub(ctx.U64,addr,ctx.Constant(ctx.U64,u64{HK_GUEST_PAGES_BASE}));
    const Id b=ctx.OpISub(ctx.U64,addr,ctx.Constant(ctx.U64,u64{HK_GUEST_PAGES_BASE2}));
    const Id in_a=ctx.OpULessThan(ctx.U1,a,ctx.Constant(ctx.U64,u64{HK_GUEST_PAGES_BYTES}));
    const Id in_b=ctx.OpULessThan(ctx.U1,b,ctx.Constant(ctx.U64,u64{HK_GUEST_PAGES_BYTES2}));
    const Id index=ctx.OpSelect(ctx.U64,in_a,ctx.OpShiftRightLogical(ctx.U64,a,ctx.Const(12U)),
        ctx.OpIAdd(ctx.U64,ctx.OpShiftRightLogical(ctx.U64,b,ctx.Const(12U)),ctx.Constant(ctx.U64,u64{HK_GUEST_PAGES_BYTES>>12})));
    return {ctx.OpLogicalOr(ctx.U1,in_a,in_b),index};
}
}
