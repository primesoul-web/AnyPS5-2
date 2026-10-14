# Project technical debt

- Native AOT integer and SIMD semantics follow [Intel SDM Volume 2](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), with [x86-64 SysV ABI](https://gitlab.com/x86-psABIs/x86-64-ABI) to [Apple ARM64 ABI](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms) reconstruction. The floating-point profile requires nearest-even rounding, preserved subnormals, masked exceptions and unobserved status flags/NaN payloads; FPCR is checked at entry. Division and aligned-access violations trap instead of reproducing guest exception delivery. Native tests and Rosetta SSE references do not qualify PS5 hardware behavior; AVX2 has independent integer-oracle tests but no x86 AVX2 execution oracle.
Entries about one library or component are in [debt](debt/), one file per library or component, under the same sections as this page. Entries that belong to none stay here.

### Build

- The bundled [SPIRV-Tools build](../../core/shader/recompiler/PatchSpirvTools.cmake) replaces the redundancy-elimination source with a build-directory copy that releases processed traversal maps. Remove this patch when the pinned revision includes [SPIRV-Tools #6962](https://github.com/KhronosGroup/SPIRV-Tools/pull/6962). System-dependency builds need the fix in their installed SPIRV-Tools package.
- Building on Windows requires a specific version of mingw - MinGW-w64 GCC 15.2.0 (`winlibs-gcc15`, `x86_64-ucrt-posix-seh`)
- Even compiled prx libraries on Windows require nearby (static linking of these dependencies causes conflicts):
  - libgcc_s_seh-1.dll
  - libstdc++-6.dll
  - libwinpthread-1.dll

### Silent stubs

Throughout the project, every function at every stage either **does exactly what it's supposed to or throws an exception**. Everywhere... except:

### Unknown function info

- AudioIn and NpSessionSignaling exports added without an implementation have assumed signatures
- `sceNetCtlGetIfStat` signature and 56-byte layout come from PPSA09030's call site and SharpProspero's `SceNetCtlIfStat`; the device kind is always 0, the byte counters are the host's totals over its up non-loopback hardware interfaces (not "since cleared"), and the not-connected result is assumed to match `sceNetCtlGetState`
- Font, Http2, Net, Ssl, SystemService and libkernel exports imported by PPSA12544 and added without an implementation have unknown signatures
- The Share, NpTrophy2, NpEntitlementAccess and WebBrowserDialog exports imported by PPSA12544's Unity plugins and added without an implementation have unknown signatures
- Acm, GameLiveStreaming, Net, NpEntitlementAccess, NpManager, NpWebApi2, VoiceQoS, WebBrowserDialog and libkernel exports imported by PPSA14632 and added without an implementation are declared without parameters

### Functional

- [Cancellation coverage](ARCHITECTURE.md#thread-cancellation) checks deferred request handling and recovery, not full asynchronous support or console ABI compatibility. Cancellation during file/socket I/O, sleep interruption, cancellation versus exit/detach, condition signal conservation under cancellation, and safe rejection of non-null invalid or reclaimed thread handles remain unverified. No console title or homebrew establishes the cleanup NIDs or buffer layout. Accepted asynchronous mode currently has no diagnostic for its deferred behavior.
- `--to-intel` guest module trampolines are covered only by a synthetic relinker test; no game title has been verified with them on Linux or Windows.
- The macOS relinker fixtures use GNU cleanup and TLS-model attributes through standard attribute syntax. C cleanup must exercise `__gcc_personality_v0`, and the initial-exec TLS access must retain that relocation model; replacing these with C++ RAII or default TLS would change the behavior under test.
- The optional native ARM64 graphics target is qualified only for the pinned source-built `gpu_cube` variant documented in [BUILD](BUILD.md#source-built-native-arm64-graphics-fixture). Its explicit pointer/layout and provider-effect contracts are tied to the reconstructed ELF hash. Mapping guards check readability, not object identity, bounds, ownership or asynchronous lifetime. Native graphics does not provide a complete game runtime, x86 exception restoration, x87/variadic formatting or arbitrary module loading. The Apple write-changed path uses byte-selective native stores where x86 uses SSE masks. No game compatibility or speedup is established.
- The checked graphics AOT profile is under qualification. Native mapping guards do not prove object type, ownership, lifetime, asynchronous stability or relocation of arbitrary guest integer addresses. Output/layout assertions need matched source evidence; internal bounded outputs and frame exclusions are enforced natively. Full current-base graphics execution and multi-frame comparison are still required before compatibility claims.
- Native AOT integer and SIMD semantics follow [Intel SDM Volume 2](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), with [x86-64 SysV ABI](https://gitlab.com/x86-psABIs/x86-64-ABI) to [Apple ARM64 ABI](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms) reconstruction. The floating-point profile requires nearest-even rounding, preserved subnormals, masked exceptions and unobserved status flags/NaN payloads; FPCR is checked at entry. Division and aligned-access violations trap instead of reproducing guest exception delivery. Native tests and Rosetta SSE references do not qualify PS5 hardware behavior; AVX2 has independent integer-oracle tests but no x86 AVX2 execution oracle.
- On macOS (MoltenVK on an Apple GPU), 16 AGC tests fail on hardware limits:
  - Metal has no 64-bit floats (the 7 agc_driver_f64_* tests and agc_driver_division_result_modifiers);

  - it has no 64-bit atomics (agc_driver_buffer_atomics*, agc_driver_global_atomics64, agc_driver_global_atomics_float, agc_driver_global_atomics_lanes);

  - MoltenVK offers no VK_KHR_shader_clock (agc_driver_shader_clock).

- agc_driver_sdwa_float_selectors fails on macOS: an f16 multiply that should give -0.0 gives +0.0. In isolation, Metal keeps the sign through each step of the same chain (half to float, multiply, the software float-to-half conversion). The cause, found on an M1 Pro: SPIRV-Cross's MSL backend computes a precise multiplication as `fma(l, r, 0)`, which turns a -0.0 product into +0.0. A fix is submitted as KhronosGroup/SPIRV-Cross#2704; it reaches this project when MoltenVK takes that SPIRV-Cross.
  - ordered not-equal is emitted as an unordered not-equal and two NaN tests on the bits, because SPIRV-Cross writes OpFOrdNotEqual as `!=`;

  - NaN tests read the bits;

  - an image atomic comes after an image-memory barrier, because Metal shows an invocation's earlier image stores to its atomics only after a texture fence;

  - subgroup results pass through a Private variable, so that SPIRV-Cross does not move them into `?:`, `&&` or `||`, where only some lanes would run them.

- The AOT provider catalog qualifies source-level identities and reviewed declarations, not arbitrary linked provider binaries. Changes to source, dependencies or facts require requalification. The caller remains responsible for the final linked implementation and per-call preconditions. The initial Apple catalog uses the provider notification size_t ABI and does not qualify the loader initialization profile for other consumers. Guest caller register bits must satisfy the selected provider ABI; source prototypes alone do not establish this. DWARF candidate recovery is limited to matching source-built C objects, scalar ABIs, a verified fixed RBP prologue and nonoverlapping fixed-location scalar/array locals. It is not stripped-binary ABI inference. Semantics follow DWARF 4 sections 2.5, 3.3, 4.1 and 5; compiler flags, source provenance and pointer lifetime remain separate obligations.
- The native `pad_input` consumer uses the original source with an explicit SysV O0 debug profile. It recovers four scalar function signatures and 26 nonoverlapping frame objects from matching DWARF; it still declares the native-pointer lifetime policy, CRT lifecycle and nontrapping constant-10 division profile. Ten imports resolve through the provider catalog, but seven entries were first qualified for this consumer and must not be counted as previously recovered knowledge. The initialization bridge remains a consumer obligation. Test-only provider adapters simulate input and failure paths; they do not establish physical-controller behavior, shared-provider completeness or PS5 compatibility. No performance advantage is claimed for this O0 input.
- The independent notification transfer probe uses unchanged `ps5-payload-dev/sdk` source with a qualified alternative compilation/CRT profile. It does not validate that project's loader or distribution. Its old-style `main()` requires an explicit source ABI; generic recovery retains the unresolved obligation. `pad_input` does not create a video output or validate a physical-controller event pump: production-provider smoke covers the state API and timer, while interaction coverage uses test-published state.
The incremental native-AOT DWARF dossier supports matching C DWARF 4 companions and fixed RBP frames only. Location lists follow DWARF 4 sections 2.6.2 and 2.17; lexical and inline scopes constrain their PC ranges. Register/value expressions, overlapping storage and missing/optimized-away variables remain qualification obligations. No optimized source signature or location expression is automatically promoted to a machine ABI or object-lifetime proof. CFA expressions, split storage, general stripped recovery and optimized internal calling-convention reconstruction remain unsupported.
