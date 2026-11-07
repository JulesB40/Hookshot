"""Build/run the actual Eden warp-test emitter on the host; optionally validate with SPIRV-Tools.
This generates authored shaders, not game data. No GPU execution is claimed.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import ctypes
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--spirv-headers',type=Path,required=True)
    p.add_argument('--spirv-val',type=Path)
    p.add_argument('--android-layout',action='store_true',help='Generate with the Android compact layout')
    p.add_argument('--jobs',type=int,default=4)
    a=p.parse_args()
    cxx=shutil.which('g++')
    if not cxx: raise SystemExit('Host g++ required')
    vk=ROOT/'gpu/vk'
    sources=sorted((vk/'eden').rglob('*.cpp'))+sorted((vk/'sirit/src').rglob('*.cpp'))+[vk/'vk_warp_test.cpp',vk/'vk_layout_test.cpp']
    out=Path(tempfile.mkdtemp(prefix='warp-spirv-',dir=ROOT/'work'))
    cache=ROOT/'work/host-warp-objects';cache.mkdir(exist_ok=True)
    flags=['-std=c++23','-O0','-fPIC','-w']
    if a.android_layout: flags+=['-D__ANDROID__']
    for h in ['algorithm','cstdio','cstdlib','cstring','functional','string','vector']: flags+=['-include',h]
    includes=[vk/'compat',ROOT/'gpu/compat',vk/'eden',vk/'sirit/include',vk/'sirit/src',a.spirv_headers.resolve()/'include',a.spirv_headers.resolve()/'include/spirv/unified1']
    flags+=['-I'+str(d) for d in includes]
    deps=hashlib.sha256((ROOT/'shared/guest_layout.h').read_bytes()+(' '.join(flags)+subprocess.check_output([cxx,'--version'],text=True)).encode())
    for directory in includes:
        for f in sorted(directory.rglob('*.h'))+sorted(directory.rglob('*.hpp')): deps.update(f.read_bytes())
    def compile_one(src):
        obj=cache/(hashlib.sha256(deps.digest()+str(src).encode()+src.read_bytes()).hexdigest()+'.o')
        if not obj.exists(): subprocess.run([cxx,*flags,'-c',str(src),'-o',str(obj)],check=True,capture_output=True)
        return obj
    objects=[]
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futures=[pool.submit(compile_one,s) for s in sources]
        for i,future in enumerate(as_completed(futures),1):
            try: objects.append(future.result())
            except subprocess.CalledProcessError as e: raise SystemExit(e.stderr.decode(errors='replace'))
            if i%20==0: print(f'Host emitter {i}/{len(sources)}',flush=True)
    lib=out/'libwarp_test.so'
    subprocess.run([cxx,'-shared','-Wl,--no-undefined',*map(str,objects),'-o',str(lib)],check=True)
    dll=ctypes.CDLL(str(lib))
    emit=dll.hookshot_vk_warp_test_spirv
    emit.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.c_size_t,ctypes.c_uint32];emit.restype=ctypes.c_size_t
    for lanes in [32,64,128]:
        n=emit(None,0,lanes)
        assert 5<n<100000,('Emitter returned no shader',lanes,n)
        words=(ctypes.c_uint32*n)();assert emit(words,n,lanes)==n
        assert words[0]==0x07230203
        at=5
        while at<n:
            count=words[at]>>16;assert count>0 and at+count<=n;at+=count
        assert at==n
        path=out/f'warp{lanes}.spv';path.write_bytes(bytes(words))
        if a.spirv_val: subprocess.run([str(a.spirv_val),'--target-env','vulkan1.1',str(path)],check=True)
        print(f'PASS: real Eden emitters generated {lanes}-lane partition test ({n} words); GPU execution not tested')
    layout=dll.hookshot_vk_layout_test_spirv
    layout.argtypes=emit.argtypes;layout.restype=ctypes.c_size_t
    from check_layout_spirv import check_layout
    for mode in (0,1):
        n=layout(None,0,mode);assert 5<n<100000
        words=(ctypes.c_uint32*n)();assert layout(words,n,mode)==n
        path=out/f'layout{mode}.spv';path.write_bytes(bytes(words))
        if a.spirv_val: subprocess.run([str(a.spirv_val),'--target-env','vulkan1.1',str(path)],check=True)
        check_layout(path,a.spirv_headers/'include/spirv/unified1/spirv.core.grammar.json',a.android_layout,bool(mode))
        fingerprint=2166136261
        for word in words: fingerprint=((fingerprint^word)*16777619)&0xffffffff
        print(f'PASS: lookup mode {mode}, {n} words, fingerprint {fingerprint:#x}; GPU execution not tested')
    print('Shaders:',out)
if __name__=='__main__': main()
