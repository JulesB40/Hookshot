#!/usr/bin/env python3
"""Build the game-free ARM64 hardware probe, using Android SDK/NDK or native Termux tools.

Every build uses a new work/android-* directory. Never runs cleanup or accesses game inputs.
"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    print("+", shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def tool(name, directory=None):
    path = str(Path(directory) / name) if directory else shutil.which(name)
    if not path or not Path(path).is_file():
        raise SystemExit(f"Missing {name}. See android/README.md for prerequisites.")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--android-jar", type=Path, required=True)
    parser.add_argument("--build-tools", type=Path, help="SDK build-tools directory; omit on Termux")
    parser.add_argument("--ndk", type=Path, help="Official NDK for Linux x86_64 builds; omit on Termux ARM64")
    parser.add_argument("--ecj", type=Path, help="Optional Eclipse Java compiler jar when javac is unavailable")
    args = parser.parse_args()
    android_jar = args.android_jar.resolve(strict=True)
    tools = args.build_tools
    aapt = tool("aapt2", tools)
    dex = tool("d8", tools)
    align = tool("zipalign", tools)
    signer = tool("apksigner", tools)
    keytool = tool("keytool")
    if args.ndk:
        if platform.system() != "Linux" or platform.machine() != "x86_64":
            raise SystemExit("This NDK path supports Linux x86_64. On the phone omit --ndk.")
        ndk_bin = args.ndk.resolve() / "toolchains/llvm/prebuilt/linux-x86_64/bin"
        compiler = tool("aarch64-linux-android28-clang", ndk_bin)
        readelf = tool("llvm-readelf", ndk_bin)
        native_flags = ["-landroid", "-lvulkan"]
    else:
        if platform.machine() not in ("aarch64", "arm64") or not os.environ.get("PREFIX", "").startswith("/data/"):
            raise SystemExit("Native builds require ARM64 Termux. Else pass --ndk for cross-compilation.")
        compiler, readelf = tool("clang"), tool("llvm-readelf")
        # Avoid Termux's libandroid wrapper: it depends on Termux's private namespace
        # library and cannot be loaded in another APK. Link the actual OS libraries.
        native_flags = ["/system/lib64/libandroid.so", "/system/lib64/libvulkan.so"]
    work = ROOT / "work"
    if work.is_symlink():
        raise SystemExit("Refusing a symlink for the build workspace.")
    work.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="android-", dir=work))
    lib = out / "lib/arm64-v8a/libhookshot_probe.so"
    lib.parent.mkdir(parents=True)
    run([compiler, "-std=c11", "-O2", "-fPIC", "-shared", "-Wall", "-Wextra", "-Werror",
         "-Wl,-z,max-page-size=16384", "-Wl,--no-undefined", "-Wl,-soname,libhookshot_probe.so",
         ROOT / "android/native/probe.c", "-o", lib, *native_flags])
    dynamic = run([readelf, "-d", lib], capture_output=True, text=True).stdout
    needed = re.findall(r"Shared library: \[(.*?)\]", dynamic)
    if not needed or set(needed) - {"libandroid.so", "libvulkan.so", "libc.so", "libm.so", "libdl.so"}:
        raise SystemExit("APK would depend on non-system libraries: " + str(needed))
    if "com.termux" in dynamic:
        raise SystemExit("Refusing native library with a Termux-only runtime path.")
    header = run([readelf, "-h", lib], capture_output=True, text=True).stdout
    if "AArch64" not in header:
        raise SystemExit("Native library is not ARM64.")
    unsigned = out / "unsigned.apk"
    run([aapt, "link", "-o", unsigned, "--manifest", ROOT / "android/AndroidManifest.xml", "-I", android_jar])
    classes = out / "classes"
    classes.mkdir()
    source = ROOT / "android/src/dev/julesb40/hookshot/MainActivity.java"
    if args.ecj:
        run([tool("java"), "-jar", args.ecj.resolve(strict=True), "-8", "-bootclasspath", android_jar, "-d", classes, source])
    else:
        run([tool("javac"), "-source", "8", "-target", "8", "-bootclasspath", android_jar, "-d", classes, source])
    dex_out = out / "dex"
    dex_out.mkdir()
    run([dex, "--min-api", "28", "--lib", android_jar, "--output", dex_out, *sorted(classes.rglob("*.class"))])
    with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as apk:
        apk.write(lib, "lib/arm64-v8a/libhookshot_probe.so")
        apk.write(dex_out / "classes.dex", "classes.dex")
    aligned = out / "aligned.apk"
    run([align, "-p", "4", unsigned, aligned])
    keys = work / "android-signing"
    if keys.is_symlink():
        raise SystemExit("Refusing symlink for signing directory.")
    keys.mkdir(mode=0o700, exist_ok=True)
    keystore = keys / "bringup.keystore"
    if keystore.is_symlink():
        raise SystemExit("Refusing symlink for signing key.")
    if not keystore.exists():
        run([keytool, "-genkeypair", "-keystore", keystore, "-storepass", "android", "-keypass", "android",
             "-alias", "bringup", "-keyalg", "RSA", "-keysize", "2048", "-validity", "3650",
             "-dname", "CN=Hookshot Android Local Debug", "-storetype", "JKS"])
    final = out / "hookshot-android-bringup.apk"
    run([signer, "sign", "--ks", keystore, "--ks-key-alias", "bringup", "--ks-pass", "pass:android",
         "--key-pass", "pass:android", "--out", final, aligned])
    run([signer, "verify", "--verbose", final])
    run([aapt, "dump", "badging", final])
    digest = hashlib.sha256(final.read_bytes()).hexdigest()
    (out / "SHA256SUMS").write_text(f"{digest}  {final.name}\n")
    print(f"\nBuilt: {final}\nSHA256: {digest}\nHardware execution NOT verified. This is a diagnostic APK, not the game.")


if __name__ == "__main__":
    main()
