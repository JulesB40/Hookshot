# Validation — 2026-11-07

## v0.8 build checkpoint

- Actual ARM64 renderer (184 sources), JNI C warnings-as-errors, Java/DEX and APK
  builds pass. Signature verified; original package/certificate, version code 8,
  zero requested permissions.
- All seven shaders pass `spirv-val --target-env vulkan1.1` plus host evaluation
  against 3,088 independent boundary/random inputs per mode in both layouts.
- Modes 5/6 exercise production numeric helpers and selected real Int64 emitters;
  the generated shaders contain no OpBitcast. This is asserted by host checks and
  audited again by the APK on phone-generated shaders.
- Host HLE memory tests/ARM64 compilation and 14 safety/cache regressions pass.
- **v0.8 phone execution pending.** The exact-driver guest Int64/global-memory
  guard remains. No game launch or physical GPU pointer dereference is claimed.

APK SHA256: `e5af673137928bdb5d3515542942f9757b2ef937d16da7cf70138da6ba1706a0`.

## User-supplied v0.7 result

[Report excerpt](device-results/oneplus12-v07-key-results.txt), supplied by user;
no direct ADB test. OnePlus CPH2581 / Android 16 / Adreno 750 / driver 0x802fa029.

| Mode | Observed rows matching all 16 words |
|---|---|
| pure32 | PASS 64/64 |
| bridge64 | FAIL 4/64 |
| numeric64 | PASS 64/64 |
| bitcast-pack | PASS 64/64 |
| bitcast-unpack | FAIL 7/64 |

Integer types in actual emitted code include width 64, signed 0. ShaderInt64 was
reported and enabled. Existing staging, warp, CPU high-address and zeroing checks
still pass. Numeric conversion tests do not establish production guest or atomic
correctness. The failed bitcast-unpack variant returned `0xffffffff` for heap
index 0. Precise root cause remains unproven.

## Historical v0.7 build checkpoint

- 184-source ARM64 renderer link with no undefined symbols, warning-clean JNI C,
  Java/DEX/APK build and signature verification pass. Same package/certificate,
  version code 7, zero requested permissions.
- All five diagnostic shaders pass `spirv-val --target-env vulkan1.1` and host
  evaluation on 3,088 boundary/random addresses per mode for both layouts.
- Host HLE allocator tests, actual Android allocator compilation and all 14
  existing safety/cache regressions pass. `git diff --check` clean.
- Exact-driver Int64/global-memory rejection compiles; no actual game shader run.
- **v0.7 phone execution pending; no numeric conversion fallback is certified.**

APK SHA256: `f854d10f24a067c55a2b56d703045c00769da419481a47db234a8b407793d223`.

## User-supplied v0.6 text report

[Complete report](device-results/oneplus12-v06-user-report.txt), pasted by the user
from the diagnostic; no direct ADB test. Device/driver match earlier runs.

| Check | Observed result |
|---|---|
| Pure32 lookup | **PASS 64/64 rows, all 16 words**. |
| Production-style bridge64 | **FAIL 4/64 rows match**. |
| Heap-base direct index | 0, correct. |
| Heap-base bridge index | `0xffff`, expected 0. |
| Below-heap roundtrip low word | `0xffff`, expected `0xffffffff`. |
| Owned staging / warp test | PASS. |
| CPU high-address / arena page zeroing | PASS. |
| Actual game launch, graphics, saves, FPS | NOT TESTED. |

Pure32 shader fingerprint `0xb3d2526f`; bridge64 `0x9d465d16`. Fingerprints are
provenance, not evidence that differing compiler-generated ID order is invalid.

## Historical v0.6 build checkpoint

- ARM64 renderer: 184 real sources linked with `--no-undefined`. JNI C with
  `-Wall -Wextra -Werror`, Java compilation, DEX and APK packaging pass.
- APK: same development certificate/package, version code 6, zero permissions;
  signature verified. **Phone installation/execution and Copy report: pending.**
- Both lookup modes validate under Vulkan 1.1. The actual generated shader is
  evaluated on 3,088 boundary/random inputs per mode; all 16-word traces agree
  with the independent CPU reference. The pure32 mode declares no 64-bit type.
- Existing 32/64/128-lane warp shaders still validate. Host HLE allocation tests,
  Android compilation and 14 existing safety/cache regressions pass.
- Production GPU lookup remains **unverified/failing in the last observed v0.5
  run**. v0.6 is diagnostic instrumentation, not a verified fix.

APK SHA256: `593fbffaba019d604d5d61ed0a897fb4e6a69462fb1bf2c2445169ce432789a8`.

## User-observed v0.5 run (2026-11-07, screenshot at 22:26)

OnePlus CPH2581 / Android 16 API 36 / Adreno 750 / Vulkan 1.3.128 /
driver `0x802fa029`. Screenshot evidence only; no direct ADB session/raw report.

| Check | Observed result |
|---|---|
| Vulkan strip / touch | Visible green strip; 11 presses. |
| Owned staging / warp compute | PASS. |
| Compact page-index GPU | **FAIL** row 2, address `0x1000000000`, hit 1, index `0xff`, expected 0. |
| Compact CPU high addresses / page zeroing | PASS. |
| Renderer module | Loaded; game runtime not started. |
| Real game graphics / saves / FPS | NOT TESTED. |

## Historical v0.5 build checkpoint

- ARM64 renderer: 184 real sources linked with `--no-undefined`; JNI build with
  `-Wall -Wextra -Werror` passed. APK signature verified, original certificate,
  same package, version code 5, zero requested permissions.
- Actual generated Android and desktop-layout shaders pass `spirv-val --target-env vulkan1.1`
  and host evaluation against 3,088 independent boundary/random address cases.
  Structural check verifies no 64-bit page-index arithmetic is emitted.
- Existing warp shaders for maximum lane counts 32/64/128 still validate.
- Host compact HLE allocation/memory tests and ARM64 compilation pass;
  all 14 existing Python safety/cache regressions pass.
- **v0.5 phone execution: pending.** This is a fix candidate for an observed GPU
  failure, not a device-verified solution or a completed game port.

APK SHA256: `19eef15753160167514fa7e43c37bd0d6c11d42571d8101893e185815d31d8af`.

## User-observed v0.4 phone result (2026-11-07, screenshot at 22:18)

OnePlus CPH2581, Android 16/API 36, Adreno 750, Vulkan 1.3.128,
driver `0x802fa029`. User-supplied screenshot evidence; no direct ADB run or raw
report retrieval was performed.

| Check | Observed result |
|---|---|
| Vulkan strip / touch | Green strip visible, touch counter 12. |
| Owned staging / warp compute | Both PASS. |
| Compact page-index GPU | **FAIL** row 2, address `0x1000000000`, hit 1, index `0x1000000`, expected 0. |
| Compact reservation | 256 GiB at `0x1632b30000`, upper GPU window `0x3f00000000`. |
| Compact high-address CPU | PASS including VAMM edges, no aliasing and zeroing. |
| Arena page/zeroing | PASS; host page 4096 bytes. |
| Renderer module | Loaded; game runtime not started. |
| Game graphics and FPS | NOT TESTED. |

## Historical v0.4 build checkpoint

- Android ARM64 renderer: all 184 actual source units compiled; shared link with
  `--no-undefined` passed. JNI C build with `-Wall -Wextra -Werror` passed.
- APK: version code 4, original development certificate, same isolated package,
  zero requested permissions, system-only native dependencies; signature verified.
- Actual compact HLE allocator executed on Linux with generated Android pointers:
  high region allocation, neighbor preservation, page zeroing/reuse, SDK VAMM
  exclusion, heap collision and invalid page-range rejection passed. The same
  HLE translation unit compiled for Android ARM64; it did not execute on phone.
- Generated lifted arithmetic/runtime tests passed and compile to ARM64 objects.
- Real Eden warp shaders (32/64/128 maximum lanes) and the new production page-index
  shader generated with Android layout all pass `spirv-val --target-env vulkan1.1`.
- All 14 existing Python safety/cache regressions passed; `git diff --check` clean.
- **v0.4 device execution: NOT TESTED yet.** No new game launch, graphics or FPS claim.

APK SHA256: `124cdb500b9382e03e18d37f25a497f5f0ae127ef546b3c0de76c9b56ec46bba`.

Details, phone reproduction and remaining limits: [PORTING_PROGRESS.md](PORTING_PROGRESS.md).

## Historical v0.3 checkpoint

The user supplied a v0.2 device screenshot: OnePlus CPH2581, Android 16/API 36,
Adreno 750, Vulkan 1.3.128. It shows a green strip, touch counter, actual renderer
module loading and a PASS for 4096-byte owned GPU transfer/readback. No task/mesh,
no host import, subgroup 64, supported range 64–128 and no stage able to request
32 lanes. The fixed 512 GiB reservation at 16 TiB failed with errno 12.

v0.3 compiles and links 183 actual renderer sources for Android ARM64 with no
unresolved symbols. The APK has version code 3, the same development certificate,
zero requested permissions and system-only native dependencies. Signature and
alignment verification pass.

### User-observed v0.3 phone run (2026-11-07, screenshot at 22:01)

The user supplied a screenshot from OnePlus CPH2581, Android 16/API 36,
Adreno 750, driver `0x802fa029`, Vulkan 1.3.128:

| Check | Observed result |
|---|---|
| Visible presentation / touch | Green strip visible; touch counter 11. |
| Owned staging | PASS, 4096 bytes, coherent memory. |
| Real Eden 32-lane partition compute test | PASS, 128 invocations: lane ID, ballots, votes, masks and shuffles. |
| Renderer module | Loaded; `mkwvk_init` export found. Game runtime not started. |
| Full guest window | 512 GiB unavailable even with relocation, errno 12. |
| Smaller diagnostic mapping | 256 GiB reserved at `0x1632b30000`; page/zeroing PASS, host page 4096 bytes. |
| Subgroup capabilities | Default 64, range 64–128; stages `0x30`, operations `0xbf`; no stage can request 32 lanes. |
| Game launch, graphics, saves, FPS | NOT TESTED. |

This is user-observed screenshot evidence, not a direct ADB test. The raw private
report has not been retrieved. The compute PASS validates the authored cases;
it does not establish general shader, divergent execution, HMMA or other-stage
correctness. The diagnostic mapping is not wired into the game. Virtual address
reservation does not mean 256 GiB of physical memory was allocated.

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
