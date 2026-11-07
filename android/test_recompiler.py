#!/usr/bin/env python3
"""Game-free smoke: upstream ARM64->C emitter, exact FP runtime, native execution and optional target compilation.

This is a small synthetic instruction test, not evidence that the game boots.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-cxx", default="clang++")
    parser.add_argument("--host-cc", default="clang")
    parser.add_argument("--target-cc", help="Optional Android NDK ARM64 clang; compile only, no device run")
    args = parser.parse_args()
    cc, cxx = shutil.which(args.host_cc), shutil.which(args.host_cxx)
    if not cc or not cxx:
        raise SystemExit("Install clang/clang++, or pass --host-cc and --host-cxx.")
    work = ROOT / "work"
    if work.is_symlink():
        raise SystemExit("Refusing symlink build workspace")
    work.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="android-recompiler-test-", dir=work))
    emitter = out / "liba64emit.so"
    run([cxx, "-std=c++20", "-O1", "-shared", "-fPIC", ROOT / "third_party/a64-kit/lift/a64emit.cpp", "-o", emitter])
    sys.path.insert(0, str(ROOT / "nx2"))
    import kit  # noqa: F401 (same vendored pipeline paths as the game lifter)
    import build_emit
    import a64emit
    import a64_rtgen
    import a64_lift
    import rtgen
    # Load our new synthetic-test build without touching the vendored library/cache.
    build_emit.build = lambda: str(emitter)
    a64_rtgen.write(str(out))
    rtgen.write(str(out))
    words = [0x91000400, 0x9B027C20, 0x1E222820]  # ADD x0,x0,#1; MUL x0,x1,x2; FADD s0,s1,s2
    texts, flags = a64emit.translate([(0x80000000 + i * 4, word) for i, word in enumerate(words)])
    if any(flags):
        raise SystemExit("Emitter failed to translate authored test instructions")
    helpers = set()
    texts = [a64_lift.stack_stores(a64_lift.pack_flags(a64_lift.widen_vstores(
        a64_lift.vectorize_fpx(a64_lift.outline_fpx(text, helpers))))) for text in texts]
    (out / "a64_fpx.h").write_text(a64_lift.fpx_header(helpers))
    source = '#include "a64.h"\n#include <stdio.h>\n'
    source += "\n".join(f"void test{i}(Ctx *c) {{\n{text}}}" for i, text in enumerate(texts))
    source += r'''
int main(void) {
    Ctx c = {0};
    c.x[0] = UINT64_MAX;
    test0(&c);
    if (c.x[0] != 0) return 1;
    c.x[1] = 17; c.x[2] = 19;
    test1(&c);
    if (c.x[0] != 323) return 2;
    c.vreg[1][0] = 0x3fc00000; /* 1.5f */
    c.vreg[2][0] = 0x40100000; /* 2.25f */
    test2(&c);
    if (c.vreg[0][0] != 0x40700000 || c.vreg[0][1] != 0) return 3;
    puts("PASS: synthetic ADD overflow, MUL, FADD and scalar vector upper-lane clearing");
    return 0;
}
'''
    fixture = out / "synthetic.c"
    fixture.write_text(source)
    common = ["-std=gnu11", "-O1", "-fno-strict-aliasing", "-ffp-contract=off", "-I" + str(out),
              "-I" + str(ROOT / "third_party/a64-kit/rt")]
    executable = out / "synthetic"
    run([cc, *common, fixture, out / "a64_suyu.c", "-lm", "-o", executable])
    run([executable])
    # Exercise the generated Android base indirection without a game dump or fixed mapping.
    relocated = out / "relocated.c"
    relocated.write_text('''#include "a64.h"
#include <stdio.h>
uint8_t *hos_android_guest_base;
int main(void) {
    uint8_t first[128] = {0}, second[128] = {0};
    hos_android_guest_base = first;
    *A64_PTR(17) = 0x5a;
    hos_android_guest_base = second;
    *A64_PTR(17) = 0xa5;
    if (first[17] != 0x5a || second[17] != 0xa5 || A64_MEM != second) return 1;
    puts("PASS: generated Android memory base follows relocation without changing the old arena");
    return 0;
}
''')
    run([cc, *common, "-D__ANDROID__", relocated, "-o", out / "relocated"])
    run([out / "relocated"])
    if args.target_cc:
        for source in (fixture, relocated, out / "a64_suyu.c"):
            run([args.target_cc, *common, "-march=armv8.2-a", "-c", source, "-o", out / (source.stem + "-arm64.o")])
        print("Android ARM64 objects compiled. They have NOT executed on the phone.")


if __name__ == "__main__":
    main()
