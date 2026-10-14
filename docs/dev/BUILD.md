# Build

## Relinker only

The relinker can be built without initializing submodules or configuring SDL, Vulkan, FFmpeg, FreeType or the shader recompiler. It requires CMake, a C++20 compiler and a build tool. Python 3 enables the Python regression tests.

```sh
cmake -S . -B build-relinker -G Ninja -DCMAKE_BUILD_TYPE=Release -DANYPS5_RELINKER_ONLY=ON -DBUILD_TESTING=ON
cmake --build build-relinker --parallel
ctest --test-dir build-relinker --output-on-failure
```

Python must be found during CMake configuration for Python tests to be registered. List registered tests with `ctest --test-dir build-relinker -N` and look for `input_magic`, a Python-backed relinker test. If automatic discovery fails, add `-DPython3_EXECUTABLE="C:/path/to/python.exe"` to the configure command, using the path to your Python interpreter, then list the tests again.

This mode builds the conversion tool and its tests on Linux, Windows and macOS, including Apple Silicon. macOS uses AppleClang from the Xcode command-line tools; Windows uses the MinGW-w64 toolchain below. The executable is `build-relinker/core/relinker/relinker` (`relinker.exe` on Windows with Ninja).

The output is x86-64 Linux ELF, Windows PE or, with `--macos`, Mach-O. Converted games need system libraries built for the target OS by the [full build](#full-build) and an x86-64 host, which on Apple silicon is Rosetta. Tests inspect every output format; execution checks run only on their compatible hosts.

Use a separate build directory for the full build.

## Experimental native Apple ARM64 compiler

`ANYPS5_NATIVE_AOT` enables an offline Apple ARM64 compiler on Apple Silicon or Linux. The default `apple-arm64` target emits Mach-O for Apple Silicon. The optional `linux-arm64` validation target emits AArch64 ELF so Linux ARM64 CI can execute compiler output natively. It reads x86-64 ELF code and explicit function/data contracts, emits LLVM IR, and optionally builds an ARM64 Mach-O object with Xcode's Clang. Generated code uses native calls and stack frames; there is no shipped x86 decoder, interpreter or JIT. This target is an initial compiler implementation, not a complete macOS game runtime.

It requires Python 3.10 or newer, Xcode command-line tools on macOS or Clang on Linux, and two pinned source submodules. Execution tests require a native ARM64 host. Linux x86-64 can cross-compile both object formats but does not run them. The regular relinker and full build keep their existing behavior when the option is off.

```sh
git submodule update --init 3rdparty/capstone 3rdparty/pyelftools
cmake -S . -B build-aot -G Ninja -DCMAKE_BUILD_TYPE=Release -DANYPS5_RELINKER_ONLY=ON -DANYPS5_NATIVE_AOT=ON -DBUILD_TESTING=ON
cmake --build build-aot --parallel
ctest --test-dir build-aot --output-on-failure --parallel 2
python3 build-aot/core/aot/native_aot.py input.elf --contracts contracts.json --output module.ll --object module.o
```

The contract has `schema: 1`, the input file's `sha256`, `functions`, `imports`, complete non-executable `data` segments, and `pointer_relocations`. Each function declares its guest address and size, native symbol name, scalar result and parameter types. Import keys are the ELF symbol names; values declare the native provider's name and signature. Relocation entries are `[guest_slot, guest_target]` pairs. The compiler checks these against the image instead of guessing an ABI. The generated object must be linked with native implementations of its imports and a native entry-point caller. It does not include PS5 system libraries, a process loader or application packaging.

Supported operations cover declared scalar integer, SSE and VEX integer/bitwise subsets. Native signatures support integer and pointer parameters, `float`/`double`, narrow integer extension and stack arguments. Direct calls and resolved tail calls use native functions. Register or memory calls require an instruction-address-bound signature and a declared compatible target set; target completeness remains the annotation author's responsibility. The recovery helper in `core/aot/recover.py` collects symbol, unwind and companion-object evidence but does not infer signatures or prove all indirect targets.

The tests build synthetic ELF inputs, compile and execute ARM64 code, check native callback/tail-call ABIs and compare supported operations with independent integer oracles and x86 SSE probes. The SSE reference executables use Rosetta on Apple Silicon; generated ARM64 programs do not. Those differential tests therefore require Rosetta to be installed. They do not qualify console hardware, complete games or performance.

### Source-built native runtime example

Initialize `3rdparty/ps5link-sdk` at its recorded commit. This optional source dependency builds the public SDK linker and startup code under its GPL-3.0 license; no third-party fixture binaries are checked in. The ordinary compiler build does not require it.

```
git submodule update --init 3rdparty/capstone 3rdparty/pyelftools 3rdparty/ps5link-sdk
cmake -S . -B build-aot -G Ninja -DCMAKE_BUILD_TYPE=Release -DANYPS5_RELINKER_ONLY=ON -DANYPS5_NATIVE_AOT=ON -DANYPS5_NATIVE_AOT_EXAMPLE=ON -DBUILD_TESTING=ON
cmake --build build-aot --target native_aot_hello_notify --parallel 3
```

On Apple Silicon this command compiles the SDK's `hello_notify` source to x86-64 ELF, derives hash-bound contracts from matched companion objects and DWARF stack locations, emits ARM64 Mach-O, links the runtime, and executes the resulting program. It checks the notification, process exit and teardown callback, an ARM64-only executable, and `sysctl.proc_translated=0`. The runtime reuses the repository's application heap, notification and timer implementations with native image registration and startup glue. Missing imports fail during contract generation or linking. No game runtime, module loader or interpreter is bundled.

The default CI source variant changes only the example's 30-second delay to 1,000 microseconds so each test remains below 30 seconds; the runtime's sleep function is not bypassed. The source hashes, function coverage, frame prologue and debug stack object must match the declared example. The generated `build-aot/core/aot/hello-notify/contracts.json`, `module.ll`, `module.optimized.ll` and `result.json` describe the actual input and result. To build and run the original delay on Apple Silicon or Linux ARM64, use `cmake --build build-aot --target native_aot_hello_notify_original`. This manual target writes to the `hello-notify/original` subdirectory and remains outside the time-limited CTest suite.

On Linux ARM64 the same target emits AArch64 ELF and executes it natively, checking the notification, exit, teardown, executable machine type and host architecture. Immutable imports use ELF RELRO and startup checks of the actual process mappings. Linux x86-64 cross-compiles the example to Apple ARM64 Mach-O without executing it. Both Linux configurations also run the native C++ image, heap, notification, timer and readonly-page tests. The ARM64 CI job runs the AOT tests only; the relinker's x86-only execution tests remain in the x86-64 job. Apple Silicon runs the full native compiler suite and the Mach-O example. Rosetta is needed only for the older x86 SSE reference tests, not for building or executing either native runtime example.

The CLI accepts `--target apple-arm64` (default) or `--target linux-arm64`; the selected target is recorded in the compilation report. Linux ARM64 exists to validate the same lowering and runtime consumer in Linux CI. Its qualification is limited to the tested scalar/provider/example paths and is not a complete additional platform port.


Guest TLS and C++ exceptions, aggregate and variadic ABIs, general computed jumps, x87, atomics, guest fault delivery, concurrent memory semantics and full module lifecycle are incomplete. Unsupported instructions and declared ABIs reject. Pointer operands need a proven native origin: declared pointer arguments/results, reconstructed image/frame addresses or tracked pointer cells. For acyclic direct calls within the image, the compiler infers writes from validated instruction states, including transitive tail calls. Private frame writes preserve caller memory proofs; fixed image writes invalidate overlapping pointer cells. For a single-entry straight-line loop, constant initialization, one positive 64-bit increment after an equality exit test, and exact reachability of the exit value can prove an unsigned index range without wraparound. The range applies only inside that loop; indexed frame writes preserve unrelated pointer cells only when their entire byte range stays inside the native frame. The report records these bounded-loop proofs. Fixed-size writes through declared pointer parameters are inferred with signed constant offsets and propagated through acyclic direct and tail calls. A known register argument or tracked stack argument resolves them to a bounded frame/image region or to a caller parameter. Overlapping ranges are combined per parameter; different parameters may alias. Unresolved caller parameters still invalidate memory proofs, while their relative writes remain available to higher-level callers. Other loop shapes, unqualified imports, indirect calls, recursive call chains and unresolved aliases retain conservative invalidation. Exact eight-byte pointer stores, spills and reloads retain their frame/image/parameter origin, including declared incoming stack parameters. Partial, wide or potentially aliasing writes invalidate every overlapping cell; unknown calls clear these origins. Control-flow joins retain only identical origins on all paths. Arbitrary pointer fields loaded from structs still reject. Inferred effects and provider-binding preconditions are included in the compilation report. Concurrent accesses are not qualified by the single-threaded memory model. See [TechnicalDebt](TechnicalDebt.md) for the remaining qualification limits.

Import contracts optionally accept `memory_effects` with exactly `writes`, `callbacks` and a nonempty `evidence` string. These are caller-verified assertions about the actual native provider and execution profile, not inferred or checked against its binary. `writes` must cover all changes to guest-visible memory, including captured aliases and globals. `callbacks: false` asserts no guest callback or reentry during the call. Private host state is excluded only when it cannot alias tracked guest memory; asynchronous mutation is outside this profile. Missing effects or possible guest callbacks invalidate memory proofs.

For example, a verified native `memset(ptr, i32, u64)` provider may declare:

```json
"memory_effects": {
  "writes": [{"parameter": 0, "size_parameter": 2}],
  "callbacks": false,
  "evidence": "Identify the provider implementation/version, source and verified execution constraints"
}
```

Each write starts at a pointer parameter and specifies either a constant unsigned byte `size` or an integer `size_parameter`. Indices are zero-based. The current analysis requires a register or tracked stack pointer argument with a proven frame/image address and a constant extent; unresolved or out-of-region destinations and unknown extents retain conservative invalidation. A zero extent performs no write. `writes: []` asserts no guest-visible writes. Known scalar stack bytes can supply null pointer arguments and constant extents, including stack-passed sizes. Partial values and conflicting control-flow paths do not prove a complete constant. An all-zero pointer argument is accepted as native null without assigning pointer provenance to arbitrary integers. Native return-pointer identity still follows the result ABI contract; return aliases are not inferred.

Provider effects apply only while the import slot retains its original binding. Writes invalidate binding proofs even when they store another native pointer. Callee binding preconditions propagate through direct, tail and declared indirect calls, including recursive dependencies, and must hold before calling code compiled under those assumptions. External native callers must honor the reported preconditions as well. Internal functions cannot override inferred effects with annotations. The report retains provider evidence for review; linking the matching provider remains the caller's responsibility.


The optional `native_runtime_registration: true` contract validates the process/libc parameter chain and allocator callback ABIs, emits a versioned native image descriptor, and registers it after pointer relocations. Link these objects with the `anyps5_native_image` static target and the actual native import providers. The registry supplies `ApplicationProcessParameters_nid_no_patch`; do not also link the ordinary executable-image implementation of that symbol. No fixed guest data address is required in the launcher. The component accepts one main image with static metadata lifetime; module loading/unloading and complete runtime integration remain outside this target. Allocator extension callbacks without a supported signature reject.

Functions may opt into `checked_stack_accesses`: each record has an instruction `address`, memory `operand` index, a declared stack object's `object_offset`, and a boolean `pointer`. The instruction must have one memory operand with a proven native frame base. The compiler emits native bounds checks before the access and traps on violation; this does not provide guest fault delivery. A `pointer: true` scalar load additionally checks eight-byte cell alignment and requires every candidate cell to contain an independently proved native pointer. Declaring a pointer table does not establish its contents. Stores conservatively invalidate the object's affected pointer proofs. Object layouts remain source/recovery contracts. The report lists these checked accesses separately from statically bounded loops; their runtime cost must be measured on each workload.

`immutable_data` optionally lists complete data segments as `{"address": ..., "evidence": "..."}`. This profile forbids writes, rebinding, unmapping or reprotection of those segments. Pointer relocations are emitted as symbolic native initializers in Mach-O `__DATA_CONST`; they are not patched by stores in the AOT constructor. The native image support library verifies readable, non-writable mappings before guest entry. Statically resolved writes and writable call effects into those ranges reject. Their pointer cells/import bindings remain invariant across calls; mutable segments retain conservative invalidation. External providers must honor the immutable-region contract. This is a constrained static-linking profile, not support for self-modifying import tables. See [LLVM global variables](https://llvm.org/docs/LangRef.html#global-variables) and [Apple dyld initialization](https://github.com/apple-oss-distributions/dyld/blob/main/doc/dyld4.md).

### Checked graphics contracts

Graphics inputs may supply instruction-bound `checked_pointer_loads`, `checked_pointer_arguments` and `checked_pointer_bases`. Each record requires source/ABI evidence, a byte extent and the relevant instruction or argument. The compiler emits native mapping checks where requested; it does not introduce a runtime decoder or interpreter. These checks establish readable host mappings, not allocation ownership, object type, lifetime or general guest-address translation. Nullability is explicit. Unannotated accesses retain the static-provenance requirements.

`pointer_outputs` declares source-qualified native-provider output fields and a `zero` or `nonnegative` status predicate. Proof is available only after an immediate matching status test and branch, with the original provider binding intact. Internal declarations additionally require `within_parameter`: an emitted native guard checks the returned field against the input's known image region or declared stack object. Independently inferred parameter-field outputs must hold on every return path; an arbitrary zero store alone does not establish a native pointer origin.

`checked_frame_exclusion` emits a guard that prevents a declared external store from touching the private frame, including copied incoming stack arguments. Image aliases remain conservative. A checked stack-access record may set `dynamic_base: true` to prove membership in its declared frame object at runtime. Neither form permits writes outside its checked range. SIMD aliases and partial register writes invalidate affected proofs.

`guard_diagnostics: true` reports the guest instruction and guard category before a trap. The object gate still rejects residual virtual-register allocations and freezes of literal poison/undef; legitimate SSA freezes inserted by LLVM loop optimization are counted separately. `debug_pointer_loads.py` collects candidates only from matched companion-object DWARF locations and their lexical/inlined scopes. Candidates still require source-profile qualification; they are not general ABI recovery.

## Full build

```sh
git submodule update --init --recursive
```

## Requirements

- x86-64, Git, CMake 3.22.1 or newer, Ninja, C++20. On Apple silicon the build is x86-64 and runs under Rosetta.
- Linux display refresh queries require SDL's XRandR support (`libxrandr-dev` on Debian/Ubuntu; `libXrandr-devel` on Fedora). Bundled SDL configuration fails without it. Keep `SDL_X11_XRANDR` enabled.
- Linux: GCC, G++, binutils. SDL's X11 backend requires X11 and Xext development headers (`libx11-dev` and `libxext-dev` on Debian/Ubuntu). Audio output needs SDL's PipeWire, PulseAudio or ALSA backend, built from their development files (`libpipewire-0.3-dev`, `libpulse-dev` and `libasound2-dev` on Debian/Ubuntu; `pipewire-devel`, `pulseaudio-libs-devel` and `alsa-lib-devel` on Fedora). SDL loads them at run time. Without any of them SDL keeps only its silent dummy and disk drivers, so configuration fails.
- Fedora: if CMake reports `Could NOT find Threads`, install `glibc-static` and `libstdc++-static` with `sudo dnf install glibc-static libstdc++-static`, then rerun configuration. The project enables static executable linking, and FFmpeg's thread check needs these libraries.
- Windows: only MinGW-w64 GCC 15.2.0 (WinLibs `x86_64-ucrt-posix-seh`, release `15.2.0posix-14.0.0-ucrt-r7`) is currently supported. Add its `mingw64/bin` directory to `PATH` before configuring.
- macOS: AppleClang from the Xcode command-line tools, and Rosetta on Apple silicon (`softwareupdate --install-rosetta`). The full build selects x86-64 itself, so configure without `CMAKE_OSX_ARCHITECTURES`. The GPU tests and converted titles need the Vulkan loader and MoltenVK as x86-64 or universal binaries, such as those of the LunarG Vulkan SDK: set `DYLD_LIBRARY_PATH=<sdk>/macOS/lib` and `VK_DRIVER_FILES` to the SDK's `MoltenVK_icd.json`. Homebrew's `vulkan-loader` and `molten-vk` on Apple silicon are arm64 only and cannot be loaded. Without a loader the GPU tests are skipped. Run the GPU tests one at a time with a timeout (`ctest -j1 --timeout 30`) and leave out `agc_driver_lds_atomics64` and the flip and display tests, which open windows: a wedged GPU can stop WindowServer and restart the Mac.
- Windows antivirus software can quarantine the executables that the relinker tests create in the temp directory, which fails `optional_plt`, `empty_tls`, `windows_address_space` and `windows_icon`. Set `TEMP` and `TMP` to a directory the scanner excludes before running `ctest`.
- FFmpeg binaries are downloaded during configuration unless `FFMPEG_PREBUILT_DIR` is set. With the WinLibs CMake, the download fails with status 60 (`SSL peer certificate or SSH remote key was not OK`) unless `SSL_CERT_FILE` names a CA bundle, for example `C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt` from Git for Windows, as in CI.

## Commands

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel
cmake --build build --target libs --parallel
```

On macOS, configure with AppleClang instead:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

`libs` is a custom target: every library under [core/libs/prx](../../core/libs/prx) is built with
`EXCLUDE_FROM_ALL`. With the default `BUILD_TESTING=OFF`, the default build does not produce them,
so the explicit `--target libs` step is required. With `BUILD_TESTING=ON`, the default full build
also builds `libs` through the `patched_libraries_tests` dependency. Titles load the patched `.prx`
files from `build/core/libs/libs`; running a title after a library change without rebuilding `libs`
therefore tests the previous binaries and can show no effect at all.

[Relinker usage and runtime layout](../user/USAGE.md).

## CMake flags

Project switches accept `ON` or `OFF`:

| Flag                             | Default | Effect                                           |
|----------------------------------|---------|--------------------------------------------------|
| `-DBUILD_TESTING=ON`             | `OFF`   | Build and register tests.                        |
| `-DANYPS5_RELINKER_ONLY=ON`      | `OFF`   | Build only the relinker and its tests, without third-party dependencies. |
| `-DANYPS5_ENABLE_SPIRV_TOOLS=ON` | `OFF`   | Enable SPIR-V validation and optimization.       |
| `-DANYPS5_ENABLE_ASAN=ON`       | `OFF`   | Build with AddressSanitizer on Linux and move the guest arena out of ASan's shadow gap. |
| `-DANYPS5_USE_SYSTEM_DEPS=ON`    | `OFF`   | Resolve SDL2, freetype, Vulkan-Headers, SPIRV-Headers, glslang and SPIRV-Tools from the system instead of the submodules. |
| `-DAPS5_ENABLE_TIMING_LOG=ON`    | `OFF`   | Compile frame timing logging.                    |
| `-DAPS5_ENABLE_TRACY=ON`         | `OFF`   | Build the [Tracy](#tracy) profiler client into the agc driver. |
| `-DAPS5_AGC_CREATE_LOG=OFF`      | `ON`    | Disable successful `sceAgcCreateShader` logging. |
| `-DAGC_BUILD_VISUAL_TEST=ON`     | `OFF`   | Build the standalone AGC SPIR-V visual test.     |

Build configuration parameters:

| Flag                                   | Value                                                              |
|----------------------------------------|--------------------------------------------------------------------|
| `-DCMAKE_BUILD_TYPE=Release`           | `Debug`, `Release`, `RelWithDebInfo`, or `MinSizeRel`.             |
| `-DCMAKE_C_COMPILER=gcc`               | C compiler name or absolute path.                                  |
| `-DCMAKE_CXX_COMPILER=g++`             | C++ compiler name or absolute path.                                |
| `-DCMAKE_C_COMPILER_LAUNCHER=ccache`   | Optional C compiler cache; requires `ccache`.                      |
| `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` | Optional C++ compiler cache; requires `ccache`.                    |
| `-DFFMPEG_PREBUILT_DIR=<path>`         | Unpacked FFmpeg package for the target platform; empty by default. |

SDL and FreeType settings forced by the root `CMakeLists.txt` cannot be overridden with `-D`.

With `ANYPS5_USE_SYSTEM_DEPS=ON` those libraries come from the system (on Debian and Ubuntu: `libsdl2-dev`, `libfreetype-dev`, `libvulkan-dev`, `spirv-headers`, `glslang-dev`, `glslang-tools` and `spirv-tools`). A missing package, Vulkan-Headers older than 1.4.310 (the agc driver uses `VK_KHR_maintenance8`), or an SDL2 without the HIDAPI joystick driver (sdl2-compat over SDL3, or a build with HIDAPI disabled, which breaks the pad and disk-audio tests), fails the configure with an error and never falls back to the submodule. The forced SDL and FreeType settings from the root `CMakeLists.txt` do not apply with the flag ON: the system SDL2 comes with whatever its distribution enabled. The prx libraries are shared objects, so SDL2 is linked from the system as a shared library; the default build keeps its own static SDL2. `ffmpeg-core`, `libjpeg-turbo`, `stb`, `VulkanMemoryAllocator` and `LibAtrac9` always stay bundled and still need `git submodule update --init`.

## Write watch tests

The macOS write watch can be built and tested separately, without submodules or the prx libraries:

```sh
cmake -S core/libs/tests/write_watch -B build/write-watch -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build/write-watch
ctest --test-dir build/write-watch --output-on-failure
```

With `CMAKE_OSX_ARCHITECTURES=x86_64` Apple silicon runs the tests through Rosetta, which is how relinked titles run; without it they run natively. The full `BUILD_TESTING` build on macOS also registers them.

## Tracy

`-DAPS5_ENABLE_TRACY=ON` builds the [Tracy](https://github.com/wolfpld/tracy) 0.14.1 client from `3rdparty/tracy` as a shared library next to the patched `.prx` files (`libTracyClient.dll`, `libTracyClient.so.0.14.1` on Linux); copy it with them. Every `PerformanceTimer` scope of libSceAgcDriver and libSceVideoOut is a zone, every presented flip ends a frame, and the queue workers and the presenter are named threads. Connect the Tracy 0.14.1 profiler to `127.0.0.1`: the client listens only on localhost and records only while a profiler is connected. Sampling, system tracing, call stacks, code transfer and Tracy's crash handler are disabled. Without the flag nothing of Tracy is built.

On Windows MinGW frees `thread_local` objects before their destructors run, so the Tracy client is built without `thread_local` destructors and never destroys its event queue: a thread that emitted a zone and ended keeps its event blocks (2 MiB each) until the process exits.

## Compute pipeline replay

Build `agc_compute_pipeline_replay` in the full build with `-DANYPS5_ENABLE_SPIRV_TOOLS=ON`; replay validates the captured module before calling Vulkan. Set `APS5_DUMP_COMPUTE_PIPELINES` to a dedicated empty directory before starting a title to save each uncached compute pipeline's final SPIR-V, descriptor layouts, push constant ranges, specialization values, subgroup size and creation flags before compilation. Files are named `compute-<identity>/capture.aps5cp`; identical records are deduplicated. Capture stops with an error at 128 records, 256 MiB total or 36 MiB per record (at most 8,388,608 SPIR-V words, or 32 MiB of shader code). Capture files contain shader code and device metadata, but no guest buffers or textures; review them before sharing.

Set `APS5_DUMP_COMPUTE_MIN_WORDS` to capture only modules at or above that SPIR-V word count, for example `100000` when investigating large NVIDIA compilations. The default is `0` (all modules); values outside `0` through `8388608` fail. The filter applies only when capture is enabled.

```sh
cmake --build build --target agc_compute_pipeline_replay
build/core/libs/prx/libSceAgcDriver/agc_compute_pipeline_replay capture.aps5cp --cache-out pipeline.cache --output fresh.json
build/core/libs/prx/libSceAgcDriver/agc_compute_pipeline_replay capture.aps5cp --cache-in pipeline.cache --output imported.json
```

On Windows use `agc_compute_pipeline_replay.exe` and prepend `build/core/libs/libs/unpatched` to the process `PATH`. `ANYPS5_GPU` selects a device by name as for the driver. The tool uses the driver's compute pipeline creation path, without recording commands, dispatching work or importing guest memory. Compile-only devices also enable supported partially-bound, variable-count and runtime-array descriptor features to replay captured layouts; ordinary runtime devices are unchanged. Binding flags are preserved independently of update-after-bind. Replay accepts the unconditional Vulkan 1.0 capabilities listed in the pinned [Vulkan registry](https://github.com/KhronosGroup/Vulkan-Headers/blob/2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350/registry/vk.xml); other capabilities must be enabled by the device target. Non-Vulkan capabilities such as Kernel and unknown capabilities fail before pipeline creation. Unsupported features, non-highest variable bindings and counts exceeding layout support fail explicitly. Unsupported inputs and mismatched cache vendor, device or UUID fail explicitly. Output files must not already exist.

Each invocation creates one pipeline. With no `--cache-in`, its application pipeline cache starts empty; `--cache-in` imports a raw cache previously written by `--cache-out` (at most 64 MiB), and `--cache-out` exports the resulting cache. The title's wrapped `vk-pipelines-*.bin` format is not accepted. It does not load or save the title's usual pipeline cache. NVIDIA and other driver caches remain uncontrolled: a fresh application cache is not a cold driver cache. JSON reports the capture identity, GPU/driver identifiers, word count, cache policy and separate wall times for shader module, layouts, pipeline creation and cache work. Device initialization is outside these timings. Pipeline statistics captures and `APS5_PIPELINE_STATS` are refused because they change compilation cost.

`--fixture` compiles a tiny synthetic compute shader with specialization, sparse descriptor binding and push constant metadata; `--write-fixture DIRECTORY` saves it without opening Vulkan. It performs no GPU work and does not reproduce a title's compile workload. With `BUILD_TESTING=ON`, `agc_compute_pipeline_capture` checks serialization and rejected inputs. With SPIRV-Tools also enabled, `agc_compute_pipeline_replay` compiles the fixture with fresh and imported application caches, verifies cache rejection, and checks required subgroup support. `agc_compute_pipeline_capture_run` verifies the existing branch-past-endpgm compute fixture's output with capture disabled and enabled, then replays the captured pipelines and checks their identities and metadata. Each test has a 30-second timeout. Runtime tests skip when Vulkan is absent; set `ANYPS5_REQUIRE_VULKAN=1` to require execution. There is no performance pass threshold.

## Pipeline statistics

Set `APS5_PIPELINE_STATS=1` to capture and print driver statistics for each newly created graphics or compute pipeline. This requires `VK_KHR_pipeline_executable_properties` and `pipelineExecutableInfo`; an unsupported device fails with an error. Statistic names and units are driver-specific. Capturing statistics can increase pipeline compilation cost. The setting is disabled by default.

## Worker sampler

Set `APS5_SAMPLE_WORKER=<file>` to sample queue worker 0 every millisecond (in practice every timer tick). Every 20 s and at exit it writes `<file>`, one `module offset self inclusive` line per sampled address, and `<file stem>.collapsed.txt`, the worker's whole stacks in the collapsed format that speedscope.app and flamegraph.pl read: one `frame;...;frame count` line per distinct stack, root first, each frame as `module+0xRVA`. A walk that ends before the thread's root starts with `[truncated]`. The `#` lines give the samples, the walk depth (64 frames), the truncated walks, the mean and longest time the worker was suspended, and the size and PE timestamp of each module, to check the binary a file is symbolized against. A frame with `.pdata` is stepped by `RtlVirtualUnwind`; any other frame by its DWARF record in `.ehfram`, the unwind data the libraries are built with. Those steps read only inside the worker's stack, and a module is indexed after the worker resumes, the first time a walk meets it.

`APS5_SAMPLE_THREADS=<file>` samples every thread of the process every 2 ms and writes the 40 hottest addresses of each busy thread to `<file>`, 12 frames deep and through `.pdata` only. Both samplers exist only on Windows; the addresses are symbolized offline, for example with `nm` on the `.prx`.

## Performance trace

Set `APS5_TRACE_FRAMES=<start>:<count>` to record `<count>` presents from present `<start>` (counted from 0), or `APS5_TRACE_SECONDS=<start>:<length>` to record from the first present at least `<start>` seconds after the first present until the first present `<length>` seconds later. Both values are whole numbers and the length is above 0; a malformed value, or both switches together, fails the first present. The window opens and closes at presents.

Inside the window the queue workers and the presenter record every `PerformanceTimer` scope and stage as a complete event named after its frame timing key: `Driver.WorkerWait`, `Driver.Packet`, `Driver.Packet.draw`, `Graphics.Draw`, `Driver.Present`, `Driver.Present.inflight_wait`, `Vulkan.Retire` and so on. A slice shorter than 20 us is only counted: its time is in the thread's counter `<thread> folded` (milliseconds per present) and in the summary. A thread writes a present's counter sample at its first slice after the next present, so the last present of the window has no sample; its time is still in the summary. Each thread allocates a buffer for 262,144 events when it starts; a full buffer stops that thread's recording and sets `trace_dropped`. When the window closes, the presenter formats the trace once and a `ProfileOutput` writer thread writes `aps5-trace-<commit>-<pid>.json.gz` (Chrome trace-event JSON, opened by ui.perfetto.dev or chrome://tracing) and appends one line to `aps5-summary.jsonl`, both in `APS5_TRACE_DIR` or the working directory. A window that is still open when the title exits writes nothing.

The summary line has `commit` and `dirty` of the build, `env` (every `APS5_*` variable; a value containing `/` or `\` is written as `<path>`), `gpu`, `driver`, `os`, `resolution` (the video output's), `window`, `presents`, `frame_ms` (p50, p90, p99 and max of the intervals between presents), `gpu_busy_ms_per_present` (the batch timestamps behind `[present]`), `worker_busy_pct` (share of the window each queue worker spends in `Driver.Packet`, `Driver.Suspend`, `Driver.EndSubmission`, `Driver.Rewind` and `Driver.Completion`), `top_phases_ms_per_frame` (the eight largest scopes and stages, without `Driver.WorkerWait`), `trace_dropped` and `trace`. A trace holds timings, scope names, counts and thread names; no guest memory, paths or strings of the title.

The shader preparation threads are traced as well, `shader prepare`, with a buffer for 32,768 events each. A wait for the GPU mutex on a traced thread is a `GpuMutex.wait.<site>` slice (`dispatch`, `draw`, `present`, `flush` and so on, as on the `[lock]` line).

The `gpu` track holds one `batch` slice per recorder batch, from the batch's timestamp pair. With `APS5_PROFILE_GPU=1` it also holds a slice per command class range, under the class names of the `[gputime]` line (`draw`, `copy`, `fill`, `present-blit`, ...); program ranges are left out. The track keeps up to 262,144 ranges; more set `trace_dropped`. The GPU clock is mapped onto the CPU clock with calibrated timestamps (`VK_KHR_calibrated_timestamps` or the EXT version, enabled only when a trace switch is set) taken when the window opens and when it closes, so the two clocks are matched at both ends of the window. A batch is added when the driver reads its timestamps, after its fence signals, so the batches of the last frames of the window, which complete after it closes, are missing. Without calibrated timestamps the trace has no `gpu` track, and the `[trace]` line says so.

With either switch set, the recorder writes a timestamp pair per batch for the whole run, as with `APS5_PROFILE_DRAW`. Without them, a `PerformanceTimer`, a GPU mutex acquisition and a read of batch timestamps each cost one more predictable branch.

## Driver thread priority

Set `APS5_THREAD_PRIORITY=high` to raise the AGC driver's queue workers and the video output presenter above the title's threads. On Linux the threads get nice `APS5_THREAD_NICE` (default -10) through rtkit; on Windows they get `THREAD_PRIORITY_HIGHEST`. `APS5_THREAD_PRIORITY=rt` asks rtkit for real-time priority `APS5_THREAD_RT` (default 10, at most rtkit's `MaxRealtimePriority`) under rtkit's policy, `SCHED_RR` by default; on Windows it sets `THREAD_PRIORITY_TIME_CRITICAL`. rtkit only grants it when the process's hard `RLIMIT_RTTIME` is at most its `RTTimeUSecMax` (200000 us by default), so a hard limit above `APS5_THREAD_RTTIME_US` (default 200000, capped at `RTTimeUSecMax`) is lowered to it and the soft limit to at most half of it. The hard limit cannot be raised again and stays lowered for the whole process even if rtkit then refuses; see [technical debt](TechnicalDebt.md#functional). There is no fallback between the modes: when rtkit or `SetThreadPriority` refuses, a queue worker reports the refusal as a driver failure, which the next submission, wait or shutdown rethrows, and the presenter's start throws it. Each raised thread logs its priority once as `[thread-priority]`. The setting is `off` by default; a value other than `off`, `high` or `rt` is reported the same way as a refusal.

## Shader recompiler

The shader recompilation logic in [core/shader/recompiler](../../core/shader/recompiler) is isolated from the rest of the project and is a pure function of its input data, designed for integration into any other project. The current CMake target also includes cache support and links a supplied runtime target, glslang, and optionally SPIRV-Tools.

## Register context tests

The x86-64 register capture and restore tests can be built separately on Linux and macOS, without submodules or the prx libraries. On macOS:

```sh
cmake -S core/libs/tests/register_context -B build/register-context -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build/register-context
ctest --test-dir build/register-context --output-on-failure
```

On Linux x86-64, omit `CMAKE_OSX_ARCHITECTURES`; the full `BUILD_TESTING` build also registers these tests. Apple silicon runs the x86-64 tests through Rosetta. They cover the unwinder's register transfer, not the complete macOS runtime or guest exception handling.


## Source-built native ARM64 graphics fixture

This optional target runs the public SDK's `gpu_cube` as an ARM64-only macOS process through Vulkan/MoltenVK. It is a source-qualified fixture, not general game compatibility. The guest CPU code comes from the x86-64 ELF through the AOT compiler. The C-compiled ARM64 executable is an independent test reference only.

The fixture stores `core/aot/examples/gpu_cube/contract-recipe.json`. Before compilation, `contract_recipe.py` verifies the input hash, a canonical digest of declared facts and guard policies, and source identities, including structured provider evidence. It derives memory-base guard sites from decoded operands and pointer-argument guard sites from resolved call ABIs. The build report records the derivation separately from the declared function/import ABIs and remaining pointer-load, stack and frame-exclusion annotations. Existing guards and compiler rejection rules still apply. The recipe does not infer object lifetime, pointee layout or missing call targets; changing a hash is not a substitute for requalifying those facts.

A qualified recipe can also be expanded independently with the AOT dependency environment active:

```sh
python3 core/aot/contract_recipe.py input.elf --recipe contract-recipe.json --source-root . --output generated-contracts.json
python3 build-aot/core/aot/native_aot.py input.elf --contracts generated-contracts.json --output module.ll --object module.o
```

The first command writes the contracts and `generated-contracts.derivation.json`; it refuses existing output paths. Source paths are relative to `--source-root`. The second command still performs the compiler's semantic validation. Expanded site counts measure generated records, not recovered ABI coverage or the number of independent manual facts.


Requirements: Apple Silicon, a logged-in macOS graphical session, Apple clang 21.0.0 (`clang-2100.3.34.2`), CMake, Ninja and Python 3. The input contract is hash-bound; another compiler revision that changes the ELF is rejected and requires contract requalification. Install a native Vulkan loader and MoltenVK; the validated driver is MoltenVK 1.4.2. Set `DYLD_LIBRARY_PATH` to the directory containing `libvulkan.1.dylib`, `SDL_VULKAN_LIBRARY` to that library, and `VK_ICD_FILENAMES` to the MoltenVK ICD JSON. These are GPU driver dependencies, not another AnyPS5 checkout. Do not set `APS5_*` diagnostic overrides for the acceptance run.

```sh
git submodule update --init 3rdparty/capstone 3rdparty/pyelftools 3rdparty/ps5link-sdk 3rdparty/SDL2 3rdparty/Vulkan-Headers 3rdparty/SPIRV-Headers 3rdparty/SPIRV-Tools 3rdparty/glslang 3rdparty/VulkanMemoryAllocator 3rdparty/ffmpeg-core 3rdparty/freetype 3rdparty/stb 3rdparty/libjpeg-turbo 3rdparty/LibAtrac9
cmake -S . -B build-graphics -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DANYPS5_NATIVE_AOT=ON -DANYPS5_NATIVE_GRAPHICS=ON -DBUILD_TESTING=OFF
cmake --build build-graphics --target native_gpu_cube_check --parallel 6
```

The final command builds the required native runtime, reconstructs the x86 ELF from pinned source, compiles all 14 declared functions to ARM64, links both executables, runs 600 frames each, verifies all completed flip arguments and teardown, then compares eight 1920x1080 GPU captures with exact RGB equality. Empty, frozen, missing or mismatched images fail. Each executable has a 30-second runtime limit. Output goes into a fresh `build-graphics/core/libs/native/graphics-runs/run-*` directory with `acceptance.json`, logs and images. Link manifests bind each executable to its input object, source manifest and runtime archive hashes; the two executions must use identical runtime archives. No x86 interpreter, decoder, JIT or Rosetta is linked into the generated program.

The fixture uses the SDK at `ea771e535378740b6a058b8e5419eb8a0e0e0ec8`. The explicit source variant corrects `sceAgcInit`'s argument, the 80-byte VideoOut attribute layout, context-array capacity and depth convention, and waits for actual flip completion before reusing a buffer. The original source stays unchanged. The SDK's licensed, pinned `.sb` shader containers are embedded with its own tool; this does not rebuild PSSL shader source. The generated source patch is kept with the build artifacts.

The source reference preserves the input compiler's contracted constant folding (`-ffp-contract=on`) and expands remaining `llvm.fmuladd` operations into separate multiply/add operations before ARM64 code generation, matching the input's SSE2 arithmetic. Compiling the reference with contraction disabled from the start changes two projection constants by 2 ULP and three silhouette pixels in one capture. The reference-policy tests reject unrecognized or remaining fusion; the image comparison has no numerical tolerance.

Configure the graphics build separately from the relinker-only/hello-notify build. `ANYPS5_NATIVE_GRAPHICS` uses the shared macOS platform implementation and builds only the static runtime closure needed here. Variadic formatting, x87-dependent math and strings, guest exception/context restoration and jump buffers are not supplied by this target; requiring them fails at link time. Native C++ exceptions used internally by the host runtime remain available. The fixture's mapping checks do not establish object lifetime or general pointer provenance for arbitrary binaries.

To run the platform regressions, build the default native targets with `cmake --build build-graphics --parallel 6`, then use `ctest --test-dir build-graphics --output-on-failure`. These cover heap/direct memory, clock, TLS, shared-pointer compare/exchange, socket/event queues, write-watch, reference arithmetic policy and image validation. The graphics acceptance command requires the desktop/GPU session and is separate from these CTest cases.

### Reusable AOT provider facts

`core/aot/provider_catalog.py` resolves imported functions using the NID, published library and module names, and both versions from the ELF dynamic tables. Import-table indices are local to an image and are not catalog keys. The Apple catalog in `core/aot/catalogs/apple-runtime.json` checks declaration and implementation hashes before returning reviewed contracts; missing identities remain explicit obligations. Call `require_complete` before using a resolution as the complete import set. A caller may instead supply separately qualified contracts for the reported missing entries. Startup, object lifetimes and final native linkage still require consumer qualification.

`core/aot/debug_candidates.py` inventories scalar C signatures and fixed frame objects from a matching DWARF 4 companion. The output remains a candidate dossier: consumers must qualify the compilation profile and original object provenance before using it. Nonstandard calling conventions, aggregate or variadic ABIs, optimized locations, overlapping objects and ambiguous executable-section mappings remain unresolved. The final AOT compiler still validates every generated contract.

### Native controller consumer

After configuring the native graphics runtime, build and run `cmake --build build --target native_pad_input_check`. This rebuilds the unchanged public `ps5link-sdk` `pad_input` source as an x86-64 ELF, recovers matching DWARF 4 scalar ABI and frame candidates, checks the reusable provider catalog, and compiles its seven functions to Apple ARM64. The qualification profile uses O0, fixed frame pointers and pinned source; it is not stripped-binary recovery. The unsigned divisions in `append_uint` remain explicitly qualified from the constant divisor and source arithmetic.

The check compares six deterministic input/error scenarios against the same C source compiled independently for ARM64. Its test-only adapters publish input through the shared Pad provider, count sleeps, and inject user/initialization errors. Read/open errors call the real provider with invalid arguments. A separate production-provider run preserves the original 625 sleeps of 16 ms. Reports record binary/object/runtime hashes, notifications, input counts, teardown and remaining manual facts. These are simulated-input and source-translation checks, not physical-controller or console validation.

For the independent-source transfer probe, obtain `samples/hello_world/main.c` from `ps5-payload-dev/sdk` revision `f5d40f02770e9b0001dd9e18056eca70b3da8b40`, retaining its GPLv3-or-later notice. After `native_pad_input_check`, run from the repository root (replace `build` with the configured native graphics build directory):

```sh
PYTHONPATH=core/aot:3rdparty/pyelftools:3rdparty/capstone/bindings/python \
LIBCAPSTONE_PATH=build/core/aot/capstone \
python3 core/aot/examples/notification_transfer.py --source /path/to/main.c \
  --sdk 3rdparty/ps5link-sdk --linker build/core/libs/native/native_pad_linker \
  --runtime build/core/libs/native/libanyps5_native_graphics.a \
  --image-runtime build/core/aot/libanyps5_native_image.a --output-parent build/transfer-runs
```

The script checks the source hash and compares AOT against the source independently compiled for ARM64. Three catalog bindings transfer. The unprototyped `main()` is rejected by generic recovery and qualified explicitly in this source profile; its request storage is checked against matching DWARF. The probe supplies declaration-only cross headers and uses the verified ps5link CRT. It does not reproduce the original payload SDK loader. Startup, the C entry signature and host string bindings remain explicit facts.

The incremental DWARF dossier is produced with `python3 core/aot/debug_candidates.py input.elf --companion matching.o --output dossier.json` using the AOT Python environment. It retains independently recovered boundaries, source signatures, fixed frame facts and PC-scoped locations when another fact is unresolved. `facts.source_abi` describes the source type; it does not qualify an optimized internal machine ABI. `storage_candidates` and `value_locations` retain DIE and PC provenance and cannot be copied into whole-function lifetime contracts. `unresolved` identifies the stage and missing qualification. Both input bytes and companion mappings are checked on each run; changing either requires recovery again. The strict `recover_debug_candidates` API remains available for existing qualified consumers.

For the optimized controller consumer, configure the native graphics build with `-DANYPS5_PAD_OPTIMIZATION=O2`, then build `native_pad_input_check`. The default remains O0. Both profiles use the unchanged pinned SDK source and the same six deterministic input scenarios, source-compiled ARM64 reference and production sleep smoke. The minimal AOT test suite additionally compiles both profiles without requiring graphics dependencies.

`core/aot/examples/sha256_transfer.py` reproduces the external component comparison. Supply the original GPL-3.0-or-later `blackbearreloaded/ps5-native-app-boilerplate` checkout at `3ada439fa044f60c8b488678577a740761385711`; no external source is copied into this repository. Use the AOT Python environment and a built native graphics runtime:

```
python3 core/aot/examples/sha256_transfer.py \
  --source-root /path/to/ps5-native-app-boilerplate \
  --sdk 3rdparty/ps5link-sdk \
  --linker build/core/libs/native/native_pad_linker \
  --runtime build/core/libs/native/libanyps5_native_graphics.a \
  --image-runtime build/core/aot/libanyps5_native_image.a \
  --optimization O2 --output-parent /path/to/fresh-component-results
```

Repeat with `--optimization O0`. The component's original source and header hashes are checked before compilation. Each profile checks 64 combinations of known/random messages and stream chunk sizes against Python hashlib and an independently compiled native C reference, including block boundaries, output guards, hexadecimal conversion and comparison errors. The wrapper adds only an unused main for ELF packaging; the test calls the converted component functions. Scoped debug locations remain candidates. The qualified C source/profile supplies ABI and lifetime preconditions; compiler dataflow still checks each actual machine frame, including the optimized early-return path.

A holdout uses the unchanged MIT-licensed musl `src/string/memcmp.c` from the `AssemblyScript/musl` mirror at `aad50fcd791e009961621ddfbe3d4c245fd689a3`. Download the source and its COPYRIGHT from that revision separately, then replace `--source-root` with `--holdout-source /path/to/memcmp.c`. The hash is checked, and 108 buffer/length/alignment cases plus alias checks run against a bytewise oracle and an independently compiled native reference. No compiler rule was added for this input.

Set `ANYPS5_AOT_SHA256_SOURCE_ROOT` and/or `ANYPS5_AOT_MEMCMP_SOURCE` when configuring the native graphics build to register the corresponding O0/O2 component tests with CTest. Build `native_pad_linker` and `anyps5_native_graphics` first. These optional tests consume caller-supplied source files and never download dependencies during configure or test execution.

Each component receipt binds the source, linked input, generated object, contracts, native libraries, runtime archives, compiler and linker identities. The same symbol-free ELF is also analyzed without its companion; that report retains missing boundaries/ABIs and is not converted into an executable contract. Automated elapsed time covers build and acceptance only. Manual qualification time and port-wide speedups are not inferred from test or annotation counts.

`python3 core/aot/bundle.py bundle-manifest.json --output /path/to/fresh-bundle` converts a closed module graph to namespaced native objects and `registry.cpp`. The manifest schema is `anyps5.aot.bundle.v1` with `main` and `modules`. Each module record has `id`, numeric `version`, relative `input` and `contracts` paths, and a `dependencies` map of module IDs to required versions. Paths remain beneath the manifest directory; input hashes must match each qualified image contract. Dependencies must match DT_NEEDED and form a reachable acyclic graph. GNU symbol version tables and ambiguous/interposed exports reject.

Functions retain their qualified scalar machine ABIs. Data imports require `data_imports` entries with `name`, byte `size` and qualification `evidence`; the provider's bounded ELF object must match. SCE bindings compare NID plus library/module names and versions rather than local encoded indices. Standard ELF bindings use the declared dependency scope and unique default-visible symbols. Code/data namespaces prevent equal guest virtual addresses in different images from becoming the same native storage.

Link the generated objects and registry.cpp with `anyps5_native_image`. `NativeAotCompiledBundle_nid_no_patch` returns the validated registry. It checks module identities, versions, reachable dependency order, data ownership and function membership; lookup specifies module, version, symbol and kind. Only the main image may register process parameters. Arbitrary runtime loading and unloading are not supported. Those inputs continue to reject until their separate lifecycle/TLS lowering is selected.

Bundle-owned lifecycle reads INIT/PREINIT/INIT_ARRAY and FINI/FINI_ARRAY from each input. Array entries must have relative relocations to qualified returning `void()` functions. PREINIT is limited to the manifest's main module. Lifecycle fields were introduced in descriptor version 2; TLS/thread ownership requires version 3. `Start()` runs main preinitializers, then each dependency's INIT followed by its INIT_ARRAY; repeated successful Start calls do not repeat initialization. `Stop()` runs modules in reverse order, each FINI_ARRAY in reverse followed by FINI, and is idempotent after completion. A failed initialization finalizes only modules whose full initialization completed, makes the bundle non-restartable and preserves the failure. Cleanup failures are reported; a partially initialized module has no automatic rollback guarantee. Explicit Stop is required; registry destruction does not invoke guest code.

A module with PT_TLS opts into `tls_model: "general-dynamic"`. TLS templates, zero-fill, alignment and DTPMOD64/DTPOFF64 pairs are checked before lowering. TLS indexes must reside in immutable native data, and resolver calls must pass one of the verified image indexes. A qualified `__tls_get_addr` import binds to `Aps5GuestTlsGetAddr_nid_no_patch` with `ptr(ptr)` ABI and `tls_resolver: true`. Cross-module TLS imports declare `size` and `evidence` in `tls_imports`; the bundle derives the owning descriptor and offset from the matching provider. The native POSIX helper reuses the existing guest TLS implementation, without executing the x86 allocation stubs. Local/initial-exec addressing and arbitrary resolver indexes remain unsupported.

Descriptor version 3 includes TLS ownership and the qualified SysV `ptr(ptr)` entry points usable by guest threads. The native graphics runtime exports `NativeAotPthreadCreate_nid_no_patch` and `NativeAotPthreadJoin_nid_no_patch`, which delegate to the existing AnyPS5 providers. This profile admits default joinable threads. A bundle lease remains held until join completes, including host TLS destruction; Stop rejects unjoined guest threads. Worker exceptions are reported by join after releasing its ownership. Main-thread TLS is released after finalizers. Detach, cancellation and alternate exit/control-transfer paths are not qualified by this profile.

An optional module `guards` record contains `policies`, the original `facts_sha256` and `sources`. Original facts/source identities are checked before binding; guard sites are then derived against the namespaced contracts. Unmatched imports require explicit `host_binding_evidence` or the existing native binding qualification. Resolver search uses the complete closed startup group and rejects interposition ambiguities. Linked DWARF can be inspected explicitly with `debug_candidates.py --linked-debug`; this avoids applying unsupported object-file TLS debug relocations and retains linked-image provenance.

### Modular native consumer

Configure the native graphics build with `ANYPS5_AOT_SHA256_SOURCE_ROOT` pointing to the pinned checkout above and either `ANYPS5_AOT_MODULE_ELF_LINKER` pointing to LLVM `ld.lld` or `ANYPS5_AOT_MODULE_ZIG` pointing to Zig 0.14.1. The ELF linker is an offline fixture tool, not a runtime dependency. Then run:

```sh
cmake --build build-graphics --target native_module_bundle_check
```

The command builds three x86-64 ELF shared images from `core/aot/examples/module_bundle`, with separate namespaces, a dependency chain, shared data, initializer/finalizer arrays and general-dynamic TLS. Linked DWARF supplies candidates under the recorded C compilation profile. The SHA-256 source remains unchanged and external. Each image is converted separately, then linked with the native registry and production AnyPS5 thread providers into an ARM64-only executable. A second executable compiles the same C sources directly for ARM64.

Four pairs of guest-created workers verify initialized/zero-filled and cross-module TLS, distinct simultaneously live blocks, shared-data binding, join results and surrounding memory guards. Digests are compared with Python hashlib, and observable output is compared with the independent C build. An injected initializer failure verifies rollback of completed modules. Both paths check initialization/finalization order, no outstanding guest threads and native process execution. Fresh artifacts and receipts go under `core/libs/native/module-runs`; the optional `native_module_bundle` CTest rebuilds the fixture and reruns acceptance without downloading external sources.

This source-qualified closed bundle does not establish arbitrary Sony PRX startup, dynamic unload, guest C++ exceptions, cancellation, a general memory-ordering model or game compatibility. Thread failure/race unit tests use controlled provider substitutes; the modular consumer separately exercises production create/join. Recorded elapsed times are execution receipts, not a comparative performance benchmark.
