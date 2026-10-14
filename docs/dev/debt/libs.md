# libs technical debt

### Unknown function info

- [SceAvPlayerVideoEx](../../../core/libs/SceTypes.hpp) (libSceAvPlayer) - frame rate field and encoding unknown; it is left zero in frame and stream info

### Functional

- The [native graphics closure](../../../core/libs/native/CMakeLists.txt) excludes `Math.cpp`, which now asserts x87 long-double storage for `__signbitl`. Apple ARM64 long double is binary64; weakening that assertion would accept a different guest ABI. The other math providers in that translation unit are also unavailable in this optional closure until the x87 providers are separated. Requiring an excluded provider fails at link time; the qualified cube does not use them.
