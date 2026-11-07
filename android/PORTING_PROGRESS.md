# Android checkpoint v0.7 — Int64 conversion isolation

The user-pasted v0.6 report establishes a useful split on OnePlus 12 / Adreno 750
with driver `0x802fa029`: **pure32 lookup PASS 64/64**, but **bridge64 FAIL 4/64**.
For `0x1000000000`, direct index is correctly 0 while the converted index is
`0xffff`. At the address immediately below the heap, a bitcast roundtrip returns
low word `0xffff` instead of `0xffffffff`. CPU arena, owned staging and warp
partition tests still PASS. Full report: [device-results/oneplus12-v06-user-report.txt](device-results/oneplus12-v06-user-report.txt).

This narrows the failure to the path involving 64-bit values/conversions. It
does not yet identify which operation or driver/compiler component is responsible.
The differing host/device shader fingerprints alone are not proof of a fault:
compiler evaluation order can change instruction/constant ID ordering.

## Implementation

The Android renderer now explicitly refuses guest Int64/global-memory shaders
on the exact tested vendor/device/driver tuple until a verified fallback exists.
It does not lower unsupported memory loads to zeros. Other driver versions are
not certified by this guard; this is a known-failure stop, not a capability test.
Existing mesh/import restrictions remain in place. Guard compilation passes;
actual guest shader execution remains unavailable.

The v0.7 APK runs five 64-row, 16-word diagnostic variants:

| Mode | Pack low/high into Int64 | Unpack Int64 |
|---|---|---|
| pure32 | No Int64 values | No Int64 values |
| bridge64 | Existing production bitcasts/conversions | Existing production bitcasts/conversions |
| numeric64 | UConvert, shifts and OR | UConvert and shift |
| bitcast-pack | Bitcast | UConvert and shift |
| bitcast-unpack | UConvert, shifts and OR | Bitcast |

These extra paths are diagnostic candidates only; no alternate conversion has
been enabled in production. The APK reports queried/enabled shaderInt64 and audits
integer widths in the actual emitted phone SPIR-V before pipeline creation. This
helps distinguish bytecode-generation issues from later execution. The report
continues to include the first mismatch columns, heap traces and shader fingerprints.

## Validation / next phone run

All five generated shaders pass Vulkan validation and 3,088 host semantic cases
per variant, for both Android and desktop layouts. The 184-source renderer links
for ARM64, JNI C builds with warnings as errors, and the APK keeps the existing
certificate/package with version code 7 and zero requested permissions. Host HLE
allocator/ARM64 compilation and 14 safety/cache regressions pass.

v0.7 has not run on the phone yet. Install it over v0.6, wait for completion, tap
**Copy report**, and paste the full result. No game files or save data are imported.
The same Termux build instructions in README.md apply; native phone compilation
has not been performed by the developer.

Physical GPU pointer dereference, guest-pool synchronization, the full Horizon
runtime, mesh/task lowering, guest HID/audio and actual game launch remain
incomplete. Pure32 diagnostic success is not proof that real game shaders work.

---

# Historical Android checkpoint v0.6 — isolate the failing lookup

The user supplied a v0.5 screenshot at 22:26: compact lookup still FAILs at row 2,
heap address `0x1000000000`, hit 1, index `0xff` instead of 0. CPU high-address,
owned staging and authored warp checks remain PASS. The v0.5 change is therefore
not an accepted device fix; the precise root cause remains unknown.

v0.6 changes the test to expose intermediate results instead of trying another
unverified arithmetic rewrite:

- `EmitGuestPageLookup32` contains the existing 32-bit-word lookup. Production's
  `EmitGuestPageLookup` calls it with bitcast words and widens its index, preserving
  the same intended behavior. This refactor does not claim production compatibility.
- A **pure32** shader has no Int64 type/capability and calls the helper directly.
- A **bridge64** shader additionally tests the production 64-bit input bitcast,
  index widening/narrowing and high word. Both run the same 64 address cases.
- Each row returns 16 words: unchanged input, hit flag, direct index, bridge
  index, bitcast roundtrip, high-word subtraction and shift, low-word page bits,
  selected constant, fixed literals, invocation ID and a final marker.
- The APK prints the heap-base trace, the first three mismatching rows, full
  64-row match counts and a fingerprint/word count of the actual emitted shader.
  Success requires all 16 words to match on every row, not just a valid hit flag.
- **Copy report** copies the displayed diagnostic to Android's clipboard on tap.
  This helps provide complete traces without a PC or file-storage permissions.

## Build/test status and phone procedure

The renderer links 184 sources for Android ARM64; JNI C builds with warnings as
errors. Both authored diagnostic shaders validate with `spirv-val`, and host
semantic checks cover 3,088 boundary/random addresses per mode. The pure32 check
asserts no 64-bit integer type is declared. Host HLE memory and 14 safety/cache
regressions still pass. These results do not establish GPU execution.

Use the existing phone-only Termux build commands in README.md, or install the
signed v0.6 APK as an update with the original development key. Do not uninstall
or clear app data. Open it, wait for the report, tap **Copy report**, then paste
the report into the conversation. Important lines start with `Lookup` and
`Compact lookup pure32/bridge64 GPU test`.

No automatic report transmission, game import or new permission is added.
Clipboard interaction and the new tests have not been exercised on the phone
by the developer. Physical GPU page-pointer dereference and guest-pool ownership
remain unverified. The full game runtime, task/mesh fallback, guest HID and audio
remain incomplete. A diagnostic PASS must not be reported as a game launch.

---

# Historical Android checkpoint v0.5 — page-index fix candidate

The user's v0.4 screenshot shows the compact high-address CPU test and page
zeroing PASS, and the existing warp/staging checks still PASS. However the GPU
page-index check **failed** at row 2: address `0x1000000000` (heap base), hit 1,
index `0x1000000`, expected 0. The compact GPU layout was therefore not validated.

Disassembly of the generated, Vulkan-valid v0.4 shader contains the expected
64-bit base constant, subtraction, logical shift and selection. This is evidence
of a runtime discrepancy, not proof of its precise driver/compiler root cause.

The production page-table lookup now computes window membership and relative
page indices using 32-bit high/low words. Windows are 4 GiB aligned and their
sizes are multiples of 4 GiB; compile-time assertions enforce those preconditions.
The low address word contributes bits 12..31; the high word contributes page
bits above them. Only the final bounded index is widened to 64 bits for the real
page-table address calculation. Out-of-window addresses remain misses.

The same helper is used by the diagnostic compute shader. Its on-device test
now checks 64 boundary/mixed addresses, including the heap's 4 GiB transition,
upper-window edges, old 508 GiB addresses and malformed high pointers. It never
uses original game data. It still does not dereference physical GPU page pointers.

## Validation and reproduction

The renderer links all 184 sources for Android ARM64 with no undefined symbols.
The JNI probe compiles with warnings as errors. APK version code 5 keeps the
same package and development certificate and requests zero permissions.

In addition to `spirv-val`, the host test now strictly evaluates the generated
straight-line lookup shader for both Android and desktop layouts on 3,088
boundary/random addresses each and checks it
against an independent address-range reference. It rejects reintroduction of
64-bit page-index arithmetic. This is a small authored-shader evaluator, not a
general SPIR-V implementation or an actual GPU test. Host HLE memory tests and
14 Python safety/cache regressions still pass.

```sh
python tests/test_warp_spirv.py --android-layout --spirv-headers work/android-deps/SPIRV-Headers --spirv-val /absolute/path/to/spirv-val --jobs 1
```

Use the existing renderer/APK build commands below or the Termux instructions in
README.md. Install v0.5 over v0.4 with the original signing key; do not uninstall
or clear app data. Record **Compact page-index GPU test** and any failing row.
The new path requires a device retest; success on Adreno is **not claimed**.

The compact arena is still experimental. Real guest addresses, physical GPU
pointer following, guest-pool synchronization and full game execution remain
unverified. Mesh/task lowering, Horizon host services, guest HID and audio still
block a playable port. No original game files, saves or installations are opened.

---

# Historical Android checkpoint v0.4 — experimental compact layout

The Android host and Vulkan translator now share `shared/guest_layout.h` instead
of independently assuming a 512 GiB window. Android uses a 256 GiB reservation;
heap base remains 64 GiB and the upper pointer-following window moves from
508–512 GiB to 252–256 GiB. Desktop retains its previous 512 GiB layout.

The SDK VAMM range moves from 192–320 GiB to 96–160 GiB on Android. GetInfo 29/30
uses these shared bounds. The Android HLE allocator skips this reserved interval,
and heap expansion refuses to overlap it or allocated regions. Generated runtime
headers use the same size for write tracking. Shader and object cache keys include
the layout header. No address bits are masked to fit an out-of-range guest pointer.

`hos_mem.c` now compiles for Android ARM64: high-page frees use POSIX discard
instead of an unconditional `VirtualFree`. Page allocate/free calls reject
out-of-arena and overflowing ranges before touching memory.

## What the v0.4 APK verifies

- It reserves the actual proposed 256 GiB layout using only fresh private mappings.
  It writes distinct markers at system/image/heap addresses, both SDK VAMM edges,
  the upper GPU window and the final page; it checks no aliasing, independent
  decommit/zeroing and rejection of an out-of-bounds commit. Only a few physical
  pages are touched, then its entire private reservation is released.
- A compute shader uses the **same production `EmitGuestPageLookup` helper** as
  Eden's global-memory pointer-following code. Sixteen runtime-supplied addresses
  cover both windows, boundary misses, the old 508 GiB address and UINT64_MAX.
  CPU checks hit/miss and every output page index. `shaderInt64` is queried and
  enabled only when supported. Lack of capability is reported explicitly.
- Existing authored warp partition and owned-staging tests remain in the APK.

The new GPU test validates page-index arithmetic, **not physical GPU pointer
dereferencing or real guest shaders**. The actual HLE allocator is exercised by
host tests and cross-compiled, not linked into this diagnostic APK.

## Validation and phone-only reproduction

Host tests execute the actual `host/hos_mem.c` with generated Android guest
pointers: allocation at the high end, neighboring writes, free/recommit zeroing,
region reuse, VAMM exclusion, heap collision rejection and invalid page ranges.
They also check desktop layout constants. Android ARM64 compilation passes.

```sh
python tests/test_compact_layout.py --target-cc /absolute/path/to/aarch64-linux-android28-clang
python tests/test_warp_spirv.py --android-layout --spirv-headers work/android-deps/SPIRV-Headers --spirv-val /absolute/path/to/spirv-val --jobs 1
```

The existing Termux renderer/APK instructions below and in README.md apply to
v0.4 without a PC. Preserve the existing signing key and build directories. The
native Termux build itself has not been executed on a phone by the developer.

Install as an update, open the app and record **Compact page-index GPU test**,
**Compact high-address CPU test**, **Compact guest arena**, plus any errors.
No game import or storage permission is needed. A v0.4 phone run is pending.

## Remaining compatibility work

This compact layout is an experimental port, not a proven replacement for the
game's original address space. A real guest may rely on fixed upper addresses or
a larger VAMM range. Such accesses must be ported or rejected, never wrapped.
VAMM mapping behavior, real GPU pointer dereference and guest-pool synchronization
remain unverified. The rest of `hos_svc.c` still depends on Win32 synchronization.

Task/mesh-to-compute/indirect lowering, coherent guest-pool upload/write-back,
remaining Horizon services, guest HID, audio and actual game launch remain
incomplete. The renderer still refuses unsupported mesh/import setup. Custom
Turnip loading, game graphics correctness and playable performance are unverified.
No original game data, installations or saves are opened by the diagnostic.

---

# Historical Android checkpoint v0.3

This remains **an incomplete game port**. The user supplied a v0.2 screenshot
confirming renderer module loading, green presentation, touch counters and owned
staging roundtrip PASS on the OnePlus 12. It also confirms no requested 32-lane
subgroups and failure of the fixed 512 GiB guest window at 16 TiB.

## New implementation

- Connect actual subgroup size/range, operation capabilities and converted stage
  masks to Eden's existing 32-lane partition path. Previously the renderer
  hardcoded maximum 32 and disabled this path. Unsupported stages fail explicitly.
- Fix wide-subgroup constant-lane broadcast to use partition-relative shuffle,
  avoiding invalid broadcast IDs when a 128-lane maximum runs at 64 lanes.
  Existing HMMA fallback uses partition-local lanes; full guest validation remains.
- Add an exported synthetic shader generator using the actual Eden lane-ID,
  ballot, vote, equality mask and index/down shuffle emitters. The APK dispatches
  128 invocations, reads 12 words per invocation and prints PASS only if every
  checked result agrees. Ballot(true) checks complete guest warps. This does not
  test arbitrary divergent shaders, fragment/vertex execution or HMMA.
- Generated Android guest code loads a relocatable memory-base pointer. Runtime
  reservation tries the preferred address, then a new kernel-selected mapping.
  All reservations remain non-overwriting. Host tests preserve occupied data.
- The APK first tests full 512 GiB relocation. If unavailable, it probes smaller
  private virtual windows and one committed page, releasing only its own mapping.
  Smaller windows are diagnostic only: the game's high address layout, heap and
  GPU tables must be redesigned before a smaller runtime arena can be used.

## Reproduce and test

Renderer/APK build commands below remain applicable. For host shader validation,
check out SPIRV-Tools tag `vulkan-sdk-1.4.309.0` and build `spirv-val` with the
matching SPIRV-Headers. Then run:

```sh
python tests/test_warp_spirv.py --spirv-headers work/android-deps/SPIRV-Headers --spirv-val /absolute/path/to/spirv-val --jobs 1
```

On the phone, install v0.3 over the existing diagnostic using the same signing key,
open it, and record the `32-lane partition GPU test` and `Guest arena` lines. No
uninstall, app-data clearing, game import or storage permission is needed.

The user supplied the v0.3 screenshot: the authored 128-invocation compute warp
test **passed on Adreno 750**, as did owned staging and page/zeroing. Full 512 GiB
reservation still failed; a 256 GiB diagnostic reservation succeeded. See
[VALIDATION.md](VALIDATION.md) for exact observed values and limits.

## Address-layout audit after the v0.3 device run

Merely halving the reservation would leave invalid guest accesses:

- `host/hos.h` fixes `HOS_SPACE_END` and `HOS_GUEST_SPACE` at 512 GiB.
  `hos_region_alloc` hands regions out from this upper bound; `hos_svc.c` reports
  the same range. A smaller host mapping alone does not change guest addresses.
- `gpu/vk/vk_memory.cpp::guest_pages_set` and Eden's
  `spirv_emit_context.cpp::DefineGlobalMemoryFunctions` both hardcode the second
  pointer-following window at `0x7f00000000` (508 GiB), outside 256 GiB.
- The heap starts at `0x1000000000` (64 GiB). A compact layout must keep heap,
  address-region allocation, SVC-reported bounds, CPU translation and GPU table
  lookup consistent. Fixed guest addresses in actual game shaders/code still
  need validation. Do not simply mask address bits or wrap them into the arena.

Next memory work requires a shared CPU/GPU layout and tests for high guest
addresses, allocation collisions and shader pointer-following. Until then the
smaller reservation remains diagnostic and the runtime refuses unsupported setup.

Remaining game blockers include task/mesh-to-compute/indirect lowering, guest-pool
synchronization without external import, full guest address layout compatibility,
Horizon host services, guest HID and audio. Turnip loading remains unsupported.
No actual game launch, correct graphics or FPS has been verified.

---

# Historical Android checkpoint v0.2

The actual Vulkan renderer now builds as `libhookshot_vk.so` for Android ARM64.
All 182 replay, Eden, Sirit, VMA and texture-decoder translation units compiled,
and the shared link passed `--no-undefined`. This is **not a playable game port**.

The user supplied a v0.1 screenshot showing a green Vulkan strip and touch presses
on OnePlus CPH2581, Android 16/API 36, Adreno 750. It reports Vulkan 1.3.128,
task/mesh unavailable, host-import unavailable, ASTC/BC supported, default subgroup
size 64. No v0.2 device run has been verified by the development host.

## New work

- Renderer Win32 helpers replaced by POSIX timing, thread naming/affinity,
  64-bit file operations and kernel-validated self-memory copies. No global
  signal handler or unsafe memcpy fallback is installed.
- Android menu/FPS text uses a public-domain bitmap font instead of GDI;
  `menu_key` injects menu input. Guest HID integration is still incomplete.
- Android disk caches/configuration are disabled until a confined app-private
  path is provided. Precompile and cache writes stop before constructing implicit
  `.running`, `.warming` or `.tmp` names. In-memory caches remain available.
- Android never deletes an arbitrary dump-trigger file. Legacy mapped-buffer
  users require coherent host-visible memory rather than silently choosing
  noncoherent memory without invalidation.
- A reusable owned-buffer primitive implements mapped staging without external
  memory import, including flush/invalidate for noncoherent allocations.
- The first POSIX guest arena is connected to the Android branch of
  `host/hos_main.c`. It never uses overwriting `MAP_FIXED`; address collisions,
  overflow and unaligned/out-of-bounds operations fail. Runtime builds must link
  `host/android/guest_arena.c`. Other Horizon services remain Windows-specific.

## What the v0.2 APK tests

The APK loads the actual renderer using `RTLD_NOW` and checks `mkwvk_init`; it
does not fabricate host callbacks or execute game code. It queries subgroup-size
control, its range and required-size shader stages, distinguishing default 64
lanes from whether 32 lanes can be requested. Shader execution at 32 lanes is not
tested.

An authored 4096-byte GPU test uploads words, copies them, fills a 1024-byte
subrange on the GPU, inserts a host-read dependency, waits for a fence, invalidates
if necessary, and checks all words. This validates transfer mechanics only;
**guest-pool capture/write ownership and synchronization are not implemented**.

The arena test temporarily reserves the 512 GiB guest window at its required
16 TiB base using non-overwriting allocation. It commits only one host page,
checks decommit/recommit zeroing, and releases its own reservation. No game files
or saves are opened. Failed reservation is reported rather than worked around
with an overwriting allocation.

The game renderer still rejects absent task/mesh and host-import support. Mesh
to compute/indirect conversion, coherent guest-pool synchronization, the remaining
Horizon runtime, audio/video and guest HID are still blockers. Custom Turnip
loading is unsupported. Game graphics and FPS remain unverified.

## Phone-only renderer build

Use the Termux prerequisites and platform jar in [README.md](README.md).
If a destination exists, reuse a verified checkout or choose a new name; do not
delete existing builds or installations.

```sh
mkdir -p work/android-deps
git clone --depth 1 --branch vulkan-sdk-1.4.309.0 https://github.com/KhronosGroup/SPIRV-Headers.git work/android-deps/SPIRV-Headers
git clone --depth 1 --branch v3.3.0 https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git work/android-deps/VulkanMemoryAllocator
python android/build_renderer.py --spirv-headers work/android-deps/SPIRV-Headers --vma work/android-deps/VulkanMemoryAllocator --jobs 1
```

Use the exact `Linked:` path as `--renderer` when building the APK:

```sh
python android/build_apk.py --android-jar "$HOOKSHOT_TOOLS/android.jar" --renderer /absolute/path/printed/by/builder/libhookshot_vk.so
```

On Linux x86_64, add `--ndk /path/to/android-ndk-r27d` to both build commands.
Termux-native compilation has not been executed on a phone here. The packager
rejects non-ARM64 libraries, Termux-only runtime paths and unexpected dependencies.

Install v0.2 as an **update**, retaining the existing signing key. Package name
is unchanged and version code increases to 2. Do not uninstall v0.1 or clear app
data. Zero permissions are requested. Insets prevent system bars covering text.

Report the module-load line, staging-roundtrip result, arena result and subgroup
control lines. A successful diagnostic is not a successful game run.

## Validation/provenance

14 existing Python regressions pass. POSIX copy tests pass on the host and
cross-compile to ARM64. Guest arena collision, alignment/bounds, zeroing and
neighbor preservation tests pass on the host; the implementation also compiles
into the ARM64 APK. No GPU/arena test result on v0.2 is claimed yet.

SPIRV-Headers: tag `vulkan-sdk-1.4.309.0`, commit
`09913f088a1197aba4aefd300a876b2ebbaa3391`. VMA: 3.3.0. The basic font is Daniel
Hepper's public-domain https://github.com/dhepper/font8x8, with its notices retained
and declaration changed to `static const unsigned char` for C++.
