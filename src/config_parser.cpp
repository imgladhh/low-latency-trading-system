#include "config_parser.h"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace llt {

TimestampNs parse_nonnegative_timestamp_arg(
    const char* value,
    const char* argument_name) {
    const std::string name = argument_name == nullptr ? "argument" : argument_name;
    const std::string accepted = " accepted range is 0.." +
        std::to_string(std::numeric_limits<TimestampNs>::max());
    if (value == nullptr || *value == '\0') {
        throw std::invalid_argument(name + " is empty;" + accepted);
    }

    const std::string_view text(value);
    TimestampNs parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed < 0) {
        throw std::invalid_argument(name + " is invalid: '" + std::string(text) + "';" + accepted);
    }
    return parsed;
}

}  // namespace llt
