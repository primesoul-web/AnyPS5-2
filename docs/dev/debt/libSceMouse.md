# libSceMouse technical debt

### Silent stubs

- [sceMouseResetAbsolutePosition](../../../core/libs/prx/libSceMouse/Export.cpp) (libSceMouse) - returns success without doing anything: no absolute position is tracked, `sceMouseRead` reports only relative deltas and `Publish` takes no absolute input, so there is nothing to reset. Unblocks PPSA30601 (#4067, #5922), which stops on its throw at start-up. No source for its signature or effect was found, only symbol lists, and console behavior with absolute positioning is unverified
