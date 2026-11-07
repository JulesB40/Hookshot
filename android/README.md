# Android ARM64 bring-up — incomplete game port

Target: OnePlus 12 / Snapdragon 8 Gen 3 / Adreno 750. **No physical device was
connected during development. No game launch, correct game frame or playable
performance has been verified.** This branch is a development checkpoint, not a
completed Android port.

The v0.5 checkpoint replaces the failing 64-bit GPU page-index chain with
32-bit-word arithmetic and expands the on-device test to 64 cases. The v0.4
CPU high-address check passed, but its GPU page-index test failed. The user's v0.3 screenshot confirms
that the authored Eden warp-partition compute test passes on Adreno 750. See [PORTING_PROGRESS.md](PORTING_PROGRESS.md).

## What the APK does

`Hookshot Android Bring-up` is a small, game-free Java/JNI APK. It creates a real
Android Vulkan surface and device, clears a swapchain image green and submits a
presentation. It reports the driver's GPU name, API version, task/mesh features,
host-import extension, compression features and subgroup size. Touch buttons,
gamepad buttons, sticks and triggers are input diagnostics, **not game controls**.
Presentation acceptance is not proof of a correct visible frame; check it visually.

The app uses `dev.julesb40.hookshot.experimental`. It has **zero requested
permissions**, no network access, no file picker, no game import and no access to
other apps' saves. Reports go to its own private `files/reports` directory. It is
debuggable for development diagnostics. No game data or generated game code is
included. Android installs it separately from existing games/emulators.

## What is implemented in the existing code

- `gpu/vk/vk_main.cpp`: Android `libvulkan.so` loading and `ANativeWindow` surface
  creation, Android progress logging, portable delay, supported composite alpha
  selection. This translation unit cross-compiles for Android ARM64; the full renderer now links in the v0.2 checkpoint.
- `gpu/vk/vk_loader.h`, `vk_platform.h`, `vk_api.h`: platform-specific surface
  function and portable shared-library export.
- `gpu/vk/vk_internal.h`: Android monotonic profiling clock.
- `nx2/zbic.py`: position-independent code for non-Windows ZBIC shared libraries.
- `prepare_lift.py`: reads modules with read-only descriptors, copies bytes into
  a new isolated workspace, records SHA-256, then optionally reuses the upstream
  unpacker and standalone lifter on those copies only. It never hard-links them.
- `test_recompiler.py`: the existing a64-kit emitter and runtime process authored
  ADD, MUL and FADD instructions. It can execute on the build host and compile
  Android ARM64 objects. This is a small smoke check, not exhaustive CPU coverage.

## Phone-only build (Termux)

Use current Termux from its [official installation sources](https://github.com/termux/termux-app#installation).
Keep source, tools and outputs in Termux private home. Do not run
`termux-setup-storage`; broad storage permission is unnecessary for the probe.
The commands below do not uninstall anything or clean existing directories.
The native Termux route is provided for device testing and has **not** been
executed on a physical phone here. It stops on missing tools or unsupported
native dependencies instead of making an APK that requires Termux's private libs.

```sh
pkg update
pkg install git python clang ndk-sysroot openjdk-21 aapt d8 apksigner vulkan-headers curl
cd "$HOME"
git clone --branch android-arm64 https://github.com/JulesB40/Hookshot.git Hookshot-Android
cd Hookshot-Android
```

If `Hookshot-Android` already exists, use that checkout or choose another **new**
directory name; do not delete or overwrite it. `aapt` supplies `aapt2` and
`zipalign`; `d8` and `apksigner` are Java tools. Check prerequisites:

```sh
command -v clang llvm-readelf javac keytool aapt2 zipalign d8 apksigner
test -r /system/lib64/libandroid.so
test -r /system/lib64/libvulkan.so
```

Only the SDK's platform jar is needed on the phone, not its x86 Linux executables.
Download the official Android 35 platform archive to a fresh tools directory,
verify its hash, and extract that single jar:

```sh
HOOKSHOT_TOOLS="$(mktemp -d "$HOME/hookshot-tools-XXXXXX")"
curl -fL https://dl.google.com/android/repository/platform-35_r02.zip -o "$HOOKSHOT_TOOLS/platform.zip"
python - "$HOOKSHOT_TOOLS" <<'PY'
import hashlib, pathlib, sys, zipfile
p = pathlib.Path(sys.argv[1])
archive = p / 'platform.zip'
expected = '0988cacad01b38a18a47bac14a0695f246bc76c1b06c0eeb8eb0dc825ab0c8e0'
if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
    raise SystemExit('SDK archive hash mismatch; stop and investigate, do not disable verification')
with zipfile.ZipFile(archive) as z, (p / 'android.jar').open('xb') as out:
    out.write(z.read('android-35/android.jar'))
PY
python android/build_apk.py --android-jar "$HOOKSHOT_TOOLS/android.jar"
```

Each build creates a new `work/android-...` directory. The script checks ELF
architecture and system-only dependencies, aligns the package, signs it with a
local development key, verifies the signature and prints the final APK path and
SHA-256. The signing key stays under `work/android-signing`; retain it to update
your own diagnostic installation. It is not a release/distribution signing key.

Open the **exact path printed after `Built:`** using:

```sh
termux-open /absolute/path/printed/by/build/hookshot-android-bringup.apk
```

Android's installer may require allowing Termux to install this APK. Complete
that normal installation prompt yourself. No storage, accessibility, root or
device-administrator permission is needed. If Android reports a signature
conflict with an existing diagnostic build, stop and recover its original key;
do not uninstall an existing app as a workaround.

## On-device verification checklist

1. Open **Hookshot Android Bring-up**. Check that no storage permission appears.
2. Check for a green strip and a report saying presentation was accepted. Record
   any Vulkan error exactly. A successful loader/device check alone is not a
   successful presentation or game test.
3. Confirm `Adreno (TM) 750` or the actual driver-reported device name, Vulkan
   version, task/mesh values, compression features and subgroup size. Do not infer
   extension support from the GPU model alone.
4. Tap several buttons, connect your controller and test buttons, sticks and
   triggers. Counters/axis values should update. Controls are diagnostic only.
5. Background/foreground the app, rotate the phone and reopen it. Record native
   crashes, freezes, missing surface recreation or disappearing input.
6. Record Android build, driver version and whether using the stock driver.
   **Custom Turnip loading is not implemented**; setting a driver environment
   variable in Termux does not change the driver of this APK.

For v0.5, also record `Compact page-index GPU test`, `Compact high-address CPU
test` and `Compact guest arena`. Install as an update using the original key;
do not uninstall or clear data. The compact layout is experimental and game
execution remains unverified.

Reports are selectable on screen. Optional phone-only log retrieval uses Android
wireless debugging and Termux's `android-tools`; no PC is needed. Enable wireless
debugging, use Android's displayed pairing and connection ports, and run:

```sh
pkg install android-tools
adb pair 127.0.0.1:PAIRING_PORT
adb connect 127.0.0.1:CONNECTION_PORT
adb shell run-as dev.julesb40.hookshot.experimental ls files/reports
adb shell run-as dev.julesb40.hookshot.experimental cat files/reports/EXACT_REPORTED_NAME.txt
adb logcat -d -s AndroidRuntime DEBUG
```

Do not use `pm clear`, `adb uninstall`, global cleanup, or commands pointed at
other package directories. Review logs before sharing; they can contain unrelated
device information. Turn wireless debugging off when done.

## Safe pipeline reuse on the phone

**Do not run the desktop `nx2/make_game.py` or `nx2/unpack.py` against original
files.** `unpack.py` rewrites modules in place and desktop code has Windows tools,
hard links and output replacement paths. This Android wrapper does not accept
NUP archives or mount original saves. For now it accepts decrypted ExeFS only.

For a dump that is already readable from Termux, choose a fresh workspace parent
under Termux private home, in a separate directory tree from the dump:

```sh
HOOKSHOT_WORK="$(mktemp -d "$HOME/hookshot-lifts-XXXXXX")"
python android/prepare_lift.py --exefs /absolute/path/to/original/exefs --workspace-parent "$HOOKSHOT_WORK"
```

This only makes independent ExeFS copies and hashes. It does not copy RomFS or
saves. To also generate C, install the lifter's Python requirements and add
`--lift` to the same command:

```sh
pkg install python-numpy
python -m venv --system-site-packages "$HOOKSHOT_WORK/venv"
"$HOOKSHOT_WORK/venv/bin/pip" install lz4 capstone
"$HOOKSHOT_WORK/venv/bin/python" android/prepare_lift.py --exefs /absolute/path/to/original/exefs --workspace-parent "$HOOKSHOT_WORK" --lift
```

These Python packages may need native builds on Termux. Stop on an installation
error; do not run untrusted binary installers. The existing pipeline builds its
a64emit and ZBIC tools locally. The full game lift needs the supported game version
documented upstream and substantial time/storage; no game dump was available to
exercise it here. Generated code stays local and must not be committed or shared.
**Generating C does not build an Android game library.** `nx2/build.py` still has
Windows link flags and runtime dependencies; do not run it expecting a working APK.

## Remaining game-port blockers

| Component | Required work |
|---|---|
| Horizon runtime | Replace Win32 memory, threading, condition variables, handles, I/O and exception handling in `host/` with Android/POSIX implementations; verify 512 GiB guest-window reservation and page sizes. |
| ARM64 build/link | Replace x86 flags (`-march=x86-64-v3`, `-mcx16`), COFF/DLL linkage and Windows libraries; compile all generated game C and runtime into an app-private native library. |
| Renderer | Port remaining Win32 synchronization, SEH, menu/window operations, memory queries and file caches in `vk_memory.cpp`, `vk_bank.cpp`, `vk_texture.cpp`, `vk_menu.cpp` and others. Compile/link the full Eden/Sirit/VMA dependency set. |
| Task/mesh shaders | Implement task payload/workgroup semantics, mesh output compaction, indices and draw counts using compute plus indirect draws when the actual driver lacks task/mesh support. Skipping draws is not a correct fallback. |
| Guest GPU memory | Implement and test coherent upload/write-back staging when `VK_EXT_external_memory_host` is missing; synchronize shader writes before CPU reads. |
| Subgroups/formats | Preserve guest 32-lane shuffle/ballot semantics, query every used format/usage and implement unsupported compression/storage and raster-state paths. |
| Lifecycle/input/audio/video | Connect input to guest HID, use Android audio/video APIs, pause safely and recreate Vulkan surfaces without use-after-free. Current controls are diagnostics only. |
| Turnip | Add a reviewed app-local custom-driver loader. Do not replace system GPU drivers. Current APK uses the system Vulkan loader. |
| Original data/saves | Add read-only document-provider imports with independent copies, confined path resolution and app-private new saves before any game execution. Never connect original saves as writable mounts. |
| Performance | Measure actual game frames, frame times, memory, thermal throttling and battery use on OnePlus 12 only after correct rendering. No FPS estimate is claimed. |

The Android renderer currently rejects missing task/mesh or host-memory import
features. Other requirements still need validation; passing those checks alone
does not establish game compatibility.

## Game-free tests and host cross-build

```sh
python -I tests/test_android_safety.py
python -I tests/test_make_game_cache.py
python android/test_recompiler.py
```

On Linux x86_64 with official SDK/NDK and a JDK:

```sh
python android/build_apk.py --android-jar /SDK/platforms/android-35/android.jar --build-tools /SDK/build-tools/35.0.0 --ndk /NDK/android-ndk-r27d
/NDK/android-ndk-r27d/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang++ -std=c++20 -O1 -DVK_NO_PROTOTYPES -DMKWVK_BUILDING -c gpu/vk/vk_main.cpp -o work/vk_main_android.o
```

`--ecj /path/to/ecj.jar` is an optional Eclipse compiler alternative to `javac`.
Host cross-compilation and APK signature verification are **not** device launch
tests. See [VALIDATION.md](VALIDATION.md) for actual results from this checkpoint.
