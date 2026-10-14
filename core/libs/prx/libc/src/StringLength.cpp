#include "SceTypes.hpp"
#include <cstring>

extern "C" {

size_t APS5_VABI strlen_nid_postfix(const char* s) {
    return std::strlen(s);
}

}
