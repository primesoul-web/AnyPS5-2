#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdlib>

extern "C" {
int APS5_VABI sceGameLiveStreamingGetProgramInfo();
}

static void Require(bool value) {
    if (!value) std::abort();
}

int main() {
    Require(sceGameLiveStreamingGetProgramInfo() == 0);
    Require(sceGameLiveStreamingGetProgramInfo() == 0);
    return 0;
}
