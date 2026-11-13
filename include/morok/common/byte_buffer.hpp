#pragma once

#include <cstddef>
#include <vector>

namespace morok::common {

// Arbitrary bytes. This type makes no claim that its contents are valid UTF-8.
using ByteBuffer = std::vector<std::byte>;

} // namespace morok::common
