#pragma once

#include "types.h"

namespace llt {

[[nodiscard]] TimestampNs parse_nonnegative_timestamp_arg(
    const char* value,
    const char* argument_name);

}  // namespace llt
