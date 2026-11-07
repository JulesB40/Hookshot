# Android checkpoint v0.2

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
