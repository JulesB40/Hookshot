# Validation — 2026-11-07

## v0.3 checkpoint

The user supplied a v0.2 device screenshot: OnePlus CPH2581, Android 16/API 36,
Adreno 750, Vulkan 1.3.128. It shows a green strip, touch counter, actual renderer
module loading and a PASS for 4096-byte owned GPU transfer/readback. No task/mesh,
no host import, subgroup 64, supported range 64–128 and no stage able to request
32 lanes. The fixed 512 GiB reservation at 16 TiB failed with errno 12.

v0.3 compiles and links 183 actual renderer sources for Android ARM64 with no
unresolved symbols. The APK has version code 3, the same development certificate,
zero requested permissions and system-only native dependencies. Signature and
alignment verification pass. This build has NOT run on the phone yet.

Host validation passed: 14 existing Python regressions, collision-safe relocated
arena/bounds/zeroing tests, stage-mask conversion, synthetic lifted arithmetic,
and generated Android memory-base indirection. Generated runtime objects compile
for Android ARM64. Real Eden warp emitters generated test shaders for maximum
subgroup sizes 32/64/128; all pass SPIRV-Tools `spirv-val --target-env vulkan1.1`.
These checks do not prove device execution or full guest shader correctness.

APK SHA256: `278d6f7eed698e2cfe62e3e22b1ca22bcbd4e8bb4d5f9c2e3e00462b89413b38`.

See [PORTING_PROGRESS.md](PORTING_PROGRESS.md) for reproduction and limitations.

## Historical v0.2 build checkpoint

All 182 actual renderer/translator sources compiled and linked for Android ARM64
with no unresolved symbols. The APK contains the actual renderer, version code 2,
zero requested permissions, and is signed with the original development key.

14 Python regressions and host POSIX memory-copy/guest-arena tests pass. New v0.2
GPU copy/readback, module loading, subgroup pinning query and guest-address-space
checks remain unverified on the phone. Game launch/graphics/FPS remain incomplete.

APK SHA256: `40611ba86e03b9107ca1b7716b289269ce3b8d368d1b69cd6fde0e478b2e0829`.

See [PORTING_PROGRESS.md](PORTING_PROGRESS.md).

## Historical v0.1 checks

Status: **partial Android bring-up, not a playable game port**.

Environment: Debian Linux x86_64, Android NDK r27d (Clang 18), Android SDK build
tools 35.0.0, platform android-35, OpenJDK 21 runtime and Eclipse ECJ 3.38.0.
No Android/ADB device or game dump was available. The phone-only Termux route has
not been executed on a phone.

| Check | Observed result |
|---|---|
| JNI diagnostic build | PASS: compiled and linked ARM64 with `-Wall -Wextra -Werror`, no unresolved symbols. |
| Native dependencies | PASS: system libraries only; no Termux-only dependencies. |
| Native ABI / ELF segments | PASS: AArch64; linker max page size set to 16 KiB. Device installation still untested. |
| Java / DEX / APK | PASS: compiled, DEX generated, packaged and aligned. |
| APK signature | PASS: `apksigner verify --verbose` accepted v3 signature. |
| Package | `dev.julesb40.hookshot.experimental`, min API 28, target API 35, `arm64-v8a` only. |
| Permissions | PASS: no requested permissions in source manifest or packaged badging. Debuggable; backups disabled. |
| Original file safety | PASS: 8 synthetic tests, including byte/inode/mtime preservation, independent copies, existing-build preservation, overlap rejection, symlinks, special files and original permission preservation. |
| Existing cache regressions | PASS: 6 tests in `tests/test_make_game_cache.py`. |
| Existing emitter and runtime | PASS on Linux x86_64: authored ADD overflow, MUL, FADD and upper-lane clearing checks. Exact existing emitter, fast-path transforms and runtime generators used. |
| Generated C on ARM64 | PASS: synthetic lifted instructions and generated FP runtime compiled to Android ARM64 objects. Not executed on ARM64. |
| Renderer platform changes | PASS: `gpu/vk/vk_main.cpp` cross-compiled to an Android ARM64 object, including Vulkan loader/surface and shared profiling header. Full renderer linking is incomplete. |
| Repository whitespace | PASS: `git diff --check`. |
| APK installation and launch | **NOT TESTED**. |
| Actual Adreno/Turnip features | **NOT TESTED**. Probe queries the device; no extension support is assumed. |
| Visible Vulkan presentation | **NOT TESTED**. APK contains a real clear/present check. |
| Touch/controller on device | **NOT TESTED**. Input diagnostics implemented; guest input not connected. |
| Full game lift/link/launch | **NOT TESTED / NOT IMPLEMENTED on Android**. |
| Correct game graphics, saves, performance, thermals | **NOT TESTED**. |

Final diagnostic artifact built from this checkpoint:

```text
hookshot-android-bringup.apk
SHA256 4d3977c747e2c99c71e265143d18098e3130a1c9ac8fd199f78b5ed846b67218
```

A local rebuild uses your own key and different ZIP timestamps, so its APK hash
will differ. The APK contains no game files. Do not interpret successful host
builds or this diagnostic's future launch as a successful game port.

Reproduction commands and the explicit remaining blockers are in [README.md](README.md).
