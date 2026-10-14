# libs technical debt

### Unknown function info

- [SceAvPlayerVideoEx](../../../core/libs/SceTypes.hpp) (libSceAvPlayer) - frame rate field and encoding unknown; it is left zero in frame and stream info

### Functional

- [CLI glibc providers](../../../core/libs/native/NativeAotGlibcMath.cpp) bridge binary64 libm arguments, the explicit x86 extended argument to nexttoward, glibc tm/timeval layouts, environment and basic string/path services. Transcendentals use Darwin libm: arbitrary bit-identical glibc libm results, unmasked exceptions, guest floating-point status and non-C locale equivalence are not established. Historical SVID math modes are excluded. Non-null gettimeofday timezone arguments and unmapped pathconf names fail explicitly. The jq qualification uses the pinned source without Oniguruma; regex support is excluded. Thread-specific keys are process-unique 32-bit IDs with per-thread values and at most four destructor passes before join completes; key deletion does not invoke destructors. Cancellation, explicit pthread_exit and detach remain unsupported.

- The [native graphics closure](../../../core/libs/native/CMakeLists.txt) excludes `Math.cpp`, which now asserts x87 long-double storage for `__signbitl`. Apple ARM64 long double is binary64; weakening that assertion would accept a different guest ABI. The other math providers in that translation unit are also unavailable in this optional closure until the x87 providers are separated. Requiring an excluded provider fails at link time; the qualified cube does not use them.

- The [glibc file compatibility providers](../../../core/libs/native/NativeAotGlibc.cpp) add LP64 fopen64/open64/stat64 aliases, bounded strncpy, strcat, rewind and two-int64 utime translation. Their file and metadata behavior is exercised by the independent bzip2 consumer. The signal alias retains the existing guest signal adapter; nested or concurrent asynchronous guest signal delivery and arbitrary signal-safe reentry remain unqualified.

- The [guest-state SCE adapter](../../../core/libs/native/NativeAotSce.cpp) qualifies explicit integer/pointer call signatures and converted callback entries. Other imports may bind real native addresses for metadata, but invoking them rejects without a qualified ABI. The closed bundle gives supplied module exports precedence over system providers and reuses the native TLS registry for DTPMOD64/general-dynamic TLS. SCE FS/GS static TLS, dynamic module unloading and arbitrary exception interoperability remain unqualified. Callback stacks are bounded to 64 KiB and thread callback registration is single-assignment. Host provider exceptions restore the dispatch scope and stack; this does not establish complete guest C++ exception semantics across every provider. The optional frame-limit observer is a harness shutdown boundary, not guest natural completion.
