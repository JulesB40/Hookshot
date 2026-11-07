#!/usr/bin/env python3
"""Build the actual Android Vulkan renderer; no game or replacement link stubs."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ndk', type=Path)
    p.add_argument('--spirv-headers', required=True, type=Path)
    p.add_argument('--vma', required=True, type=Path)
    p.add_argument('--jobs', type=int, default=2)
    a = p.parse_args()
    if not 1 <= a.jobs <= 8: raise SystemExit('Use 1-8 jobs; start with 1 on the phone')
    if a.ndk:
        cc = a.ndk.resolve() / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang++'
        libraries = ['-ldl', '-landroid']
    else:
        if platform.machine() not in ('aarch64', 'arm64') or not os.environ.get('PREFIX', '').startswith('/data/'):
            raise SystemExit('Native builds require ARM64 Termux; else pass --ndk')
        cc = Path(shutil.which('clang++') or 'clang++')
        libraries = ['/system/lib64/libdl.so', '/system/lib64/libandroid.so']
    for f in (a.vma / 'include/vk_mem_alloc.h', a.spirv_headers / 'include/spirv/unified1/spirv.hpp'):
        if not f.is_file(): raise SystemExit('Missing dependency: ' + str(f))
    vk = ROOT / 'gpu/vk'
    includes = [vk / 'compat', ROOT / 'gpu/compat', vk / 'eden', ROOT / 'gpu/third_party/suyu',
        vk / 'sirit/include', vk / 'sirit/src', a.spirv_headers.resolve() / 'include',
        a.spirv_headers.resolve() / 'include/spirv/unified1', a.vma.resolve() / 'include', vk]
    sources = sorted(vk.glob('*.cpp')) + sorted((vk / 'eden').rglob('*.cpp')) + sorted((vk / 'sirit/src').rglob('*.cpp'))
    sources += [ROOT / 'gpu/third_party/suyu/video_core/textures' / f for f in ('decoders.cpp', 'astc.cpp')]
    work = ROOT / 'work'
    if work.is_symlink(): raise SystemExit('Refusing symlink workspace')
    work.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='android-renderer-', dir=work))
    cache = work / 'android-renderer-objects'
    if cache.is_symlink(): raise SystemExit('Refusing symlink cache')
    cache.mkdir(exist_ok=True)
    flags = ['-std=c++20', '-O1', '-g0', '-fPIC', '-ffp-contract=off', '-DNDEBUG', '-DVK_NO_PROTOTYPES', '-DMKWVK_BUILDING', '-DMKWVK_INCLUDES=2']
    for h in ('algorithm', 'cstdio', 'cstdlib', 'cstring', 'functional', 'string', 'vector'): flags += ['-include', h]
    flags += ['-I' + str(i) for i in includes]
    def headers(dirs):
        digest = hashlib.sha256((ROOT / 'shared/guest_layout.h').read_bytes() + (str(cc.resolve()) + ' '.join(flags)).encode() + subprocess.check_output([str(cc), '--version']))
        for d in dirs:
            for h in sorted(d.rglob('*.h')) + sorted(d.rglob('*.hpp')): digest.update(str(h).encode() + h.read_bytes())
        return digest.digest()
    libkey = headers(includes[:6] + [a.spirv_headers / 'include'])
    backendkey = headers(includes[:6] + [a.spirv_headers / 'include', a.vma / 'include', ROOT / 'gpu/render', vk])
    stamp = hashlib.sha256(libkey)
    for f in sources + [vk / 'vk_state.h', vk / 'vk_api.h']:
        if f.is_relative_to(vk / 'eden') or f.is_relative_to(vk / 'sirit') or f.name in ('vk_shader.cpp', 'vk_state.h', 'vk_api.h'): stamp.update(f.read_bytes())
    def compile_one(src):
        islib = src.is_relative_to(vk / 'eden') or src.is_relative_to(vk / 'sirit')
        extra = ['-DHOOKSHOT_SHADER_STAMP=0x' + stamp.hexdigest()[:16] + 'ull'] if src.name == 'vk_shader.cpp' else []
        key = hashlib.sha256((libkey if islib else backendkey) + ' '.join(extra).encode() + str(src).encode() + src.read_bytes()).hexdigest()
        obj = cache / (key + '.o')
        if not obj.exists():
            tmp = out / (key + '.o')
            r = subprocess.run([str(cc), *flags, *extra, '-c', str(src), '-o', str(tmp)], capture_output=True, text=True)
            if r.returncode: raise RuntimeError(str(src.relative_to(ROOT)) + '\n' + r.stderr)
            os.replace(tmp, obj)
        return obj
    objects, errors = [], []
    print(f'Compiling {len(sources)} real renderer/translator sources', flush=True)
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futures = [pool.submit(compile_one, s) for s in sources]
        for i, future in enumerate(as_completed(futures), 1):
            try: objects.append(future.result())
            except Exception as e: errors.append(str(e)); print(e, flush=True)
            if i % 20 == 0: print(f'Compiled/checked {i}/{len(sources)}', flush=True)
    if errors:
        (out / 'failures.txt').write_text('\n\n'.join(errors))
        raise SystemExit(f'{len(errors)} translation units failed; see {out}/failures.txt')
    rsp = out / 'objects.rsp'
    rsp.write_text('\n'.join('"' + str(o) + '"' for o in objects))
    so = out / 'libhookshot_vk.so'
    subprocess.run([str(cc), '-shared', '-static-libstdc++', '-Wl,--no-undefined', '-Wl,-z,max-page-size=16384',
        '-Wl,-soname,libhookshot_vk.so', '@' + str(rsp), *libraries, '-o', str(so)], check=True)
    print(f'Linked: {so}\nRenderer only: game runtime/device execution remain unverified.')

if __name__ == '__main__': main()
