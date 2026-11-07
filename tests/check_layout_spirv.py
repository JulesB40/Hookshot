"""Small strict evaluator for the authored, straight-line page-index shader.
Not a general SPIR-V interpreter or evidence of actual GPU execution.
"""
import json
from pathlib import Path
import random
import struct

def check_layout(path,grammar_path,android,bridge=0):
    grammar=json.loads(Path(grammar_path).read_text())
    names={i['opcode']:i['opname'] for i in grammar['instructions']}
    words=struct.unpack('<'+'I'*(Path(path).stat().st_size//4),Path(path).read_bytes())
    instructions=[];at=5
    while at<len(words):
        count=words[at]>>16; assert count and at+count<=len(words)
        instructions.append((names[words[at]&65535],list(words[at+1:at+count])))
        at+=count
    types={};constants={};variables={};body=[];inside=False
    for op,a in instructions:
        if op=='OpTypeInt':types[a[0]]=('int',a[1])
        elif op=='OpTypeBool':types[a[0]]=('bool',1)
        elif op=='OpTypeVector':types[a[0]]=('vector',a[1],a[2])
        elif op=='OpConstant':constants[a[1]]=sum(v<<(32*i) for i,v in enumerate(a[2:]))
        elif op in ('OpConstantTrue','OpConstantFalse'):constants[a[1]]=op=='OpConstantTrue'
        elif op=='OpVariable':variables[a[1]]=a[2]
        elif op=='OpFunction':
            assert not inside and not body,'Expected exactly one function'
            inside=True
        elif op=='OpFunctionEnd':inside=False
        elif inside:body.append((op,a))
    if not bridge: assert ('int',64) not in types.values(),'Pure32 test must not declare 64-bit integers'
    # Regression: this lookup must not reintroduce 64-bit arithmetic/select chains.
    for op,a in body:
        if bridge in (0,1) and op.startswith(('OpI','OpShift','OpBitwise','OpSelect')) and op!='OpIEqual':
            assert types.get(a[0])!=('int',64),('64-bit arithmetic reintroduced',op)
    def run(addr):
        env=dict(constants);data=[addr&0xffffffff,addr>>32]+[0xcdcdcdcd]*14
        env.update({ident:('ptr',storage,()) for ident,storage in variables.items()})
        def value(ident):return env[ident]
        def mask(typ,x):
            return x&((1<<types[typ][1])-1) if types.get(typ,(None,))[0]=='int' else x
        for op,a in body:
            if op in ('OpLabel','OpReturn'):continue
            if op=='OpStore':
                ptr=value(a[0]);assert ptr[1]==12 and len(ptr[2])==2 and ptr[2][0]==0
                data[ptr[2][1]]=value(a[1]);continue
            typ,result,*args=a
            v=[value(x) for x in args] if op not in ('OpCompositeExtract',) else []
            if op=='OpLoad':
                ptr=v[0]
                if ptr[1]==1:x=(0,0,0) # invocation zero; one runtime address at a time
                else:
                    assert ptr[1]==12 and len(ptr[2])==2 and ptr[2][0]==0
                    x=data[ptr[2][1]]
            elif op=='OpAccessChain':x=('ptr',v[0][1],v[0][2]+tuple(v[1:]))
            elif op=='OpCompositeConstruct':x=tuple(v)
            elif op=='OpCompositeExtract':x=value(args[0])[args[1]]
            elif op=='OpBitcast':
                src=v[0]
                if types[typ][0]=='vector':x=(src&0xffffffff,src>>32)
                else:x=src[0]|src[1]<<32
            elif op=='OpUConvert':x=v[0]
            elif op=='OpIAdd':x=v[0]+v[1]
            elif op=='OpISub':x=v[0]-v[1]
            elif op=='OpIMul':x=v[0]*v[1]
            elif op=='OpBitwiseOr':x=v[0]|v[1]
            elif op=='OpShiftLeftLogical':x=v[0]<<v[1]
            elif op=='OpShiftRightLogical':x=v[0]>>v[1]
            elif op=='OpULessThan':x=v[0]<v[1]
            elif op=='OpUGreaterThanEqual':x=v[0]>=v[1]
            elif op=='OpLogicalOr':x=v[0] or v[1]
            elif op=='OpLogicalAnd':x=v[0] and v[1]
            elif op=='OpSelect':x=v[1] if v[0] else v[2]
            else:raise AssertionError('Unsupported authored-shader opcode '+op)
            env[result]=mask(typ,x)
        return data[2:]
    end=1<<(38 if android else 39);first=64<<30;upper=end-(4<<30)
    samples=[0,first-1,first,first+1,first+(1<<32),first+(8<<30)-1,first+(8<<30),
        upper-1,upper,upper+1,upper+4096,end-4096,end-1,end,0x7f00000000,(1<<64)-1]
    rng=random.Random(0x750)
    for _ in range(1024):
        samples.extend([first+rng.randrange(8<<30),upper+rng.randrange(4<<30),rng.randrange(1<<64)])
    for address in samples:
        if first<=address<first+(8<<30):expected=(address-first)//4096
        elif upper<=address<end:expected=(8<<30)//4096+(address-upper)//4096
        else:expected=None
        lo=address&0xffffffff;hi=address>>32;delta=(hi-16)&0xffffffff
        hit=int(expected is not None);index=expected if expected is not None else 0xffffffff
        trace=[hit,index,index if bridge else 0xf00d0000,lo,hi,delta,(delta<<20)&0xffffffff,lo>>12,
            0x12345678 if hit else 0xabcdef01,0xffffffff,0x12345678,0,0,0x75000007]
        assert run(address)==trace,(hex(address),run(address),trace)
    print(f'PASS: generated page-index shader semantics on {len(samples)} boundary/random addresses; no 64-bit page arithmetic; GPU execution not tested')
