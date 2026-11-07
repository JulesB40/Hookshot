"""Test real HLE memory allocator using generated Android pointers, without game files."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target-cc',help='Optional Android ARM64 clang; compile actual HLE allocator only')
    a=p.parse_args()
    cc=shutil.which('gcc')
    if not cc: raise SystemExit('gcc required for host validation')
    out=Path(tempfile.mkdtemp(prefix='compact-layout-',dir=ROOT/'work'))
    sys.path.insert(0,str(ROOT/'nx2'))
    import rtgen
    import a64_rtgen
    import build_emit
    emitter=out/'liba64emit.so'
    subprocess.run(['g++','-std=c++20','-O1','-shared','-fPIC',ROOT/'third_party/a64-kit/lift/a64emit.cpp','-o',emitter],check=True)
    build_emit.build=lambda: str(emitter)
    a64_rtgen.write(out)
    import a64_lift
    (out/"a64_fpx.h").write_text(a64_lift.fpx_header(set()))
    rtgen.write(out)
    flags=['-std=gnu11','-O1','-D__ANDROID__','-ffunction-sections','-fdata-sections',
        '-I'+str(out),'-I'+str(ROOT/'host'),'-I'+str(ROOT/'third_party/a64-kit/rt')]
    subprocess.run([cc,*flags,ROOT/'tests/test_compact_guest.c',ROOT/'host/hos_mem.c',
        ROOT/'host/android/guest_arena.c','-Wl,--gc-sections','-lpthread','-lm','-o',out/'test'],check=True)
    subprocess.run([out/'test'],check=True)
    desktop=out/'desktop.c'
    desktop.write_text('#include "'+str(ROOT/'shared/guest_layout.h')+'"\n#include <assert.h>\n'
        'int main(void) { _Static_assert(HK_GUEST_BYTES==(1ULL<<39),"desktop size changed");'
        '_Static_assert(HK_GUEST_PAGES_BASE2==0x7f00000000ULL,"desktop upper window changed");'
        '_Static_assert(HK_GUEST_VAMM_BASE==(192ULL<<30) && HK_GUEST_VAMM_BYTES==(128ULL<<30),"desktop VAMM changed");'
        'assert(hk_guest_page_index(HK_GUEST_PAGES_BASE2)==(8ULL<<30>>12));return 0;}')
    subprocess.run([cc,desktop,'-o',out/'desktop'],check=True)
    subprocess.run([out/'desktop'],check=True)
    print('PASS: desktop 512 GiB layout and page-window base preserved')
    if a.target_cc:
        subprocess.run([a.target_cc,*flags,'-c',ROOT/'host/hos_mem.c','-o',out/'hos_mem-arm64.o'],check=True)
        print('PASS: actual compact HLE memory allocator compiles for Android ARM64; not executed on phone')
if __name__=='__main__':main()
