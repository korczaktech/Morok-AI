#include "morok/common/build_info.hpp"
#ifndef MOROK_VERSION_STRING
#define MOROK_VERSION_STRING "0.0.0"
#endif
namespace morok::common {
std::string_view version() noexcept { return MOROK_VERSION_STRING; }
}
