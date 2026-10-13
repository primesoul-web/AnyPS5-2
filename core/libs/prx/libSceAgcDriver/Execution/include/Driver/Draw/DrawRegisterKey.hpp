#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_DRIVER_DRAW_DRAWREGISTERKEY_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_DRIVER_DRAW_DRAWREGISTERKEY_HPP

#include "prx/libSceAgcDriver/Execution/include/QueueState.hpp"

namespace AgcDriver::DriverDetail {

struct RegisterStateKeyParts {
    std::uint64_t stableRegisters = 0;
    std::uint64_t userDataRegisters = 0;
    std::uint64_t auxiliaryRegisters = 0;
};

std::uint64_t RegisterStateKey(const QueueState& queue, bool allUserWords = false, bool incremental = true, RegisterStateKeyParts* parts = nullptr);

}

#endif
