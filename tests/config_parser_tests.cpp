#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config_parser.h"

namespace {

bool expect_invalid(const char* value) {
    try {
        (void) llt::parse_nonnegative_timestamp_arg(value, "passive_cancel_after_ns");
    } catch (const std::invalid_argument& error) {
        const std::string message = error.what();
        return message.find("passive_cancel_after_ns") != std::string::npos &&
            message.find("accepted range") != std::string::npos;
    }
    return false;
}

}  // namespace

int main() {
    const bool ok =
        llt::parse_nonnegative_timestamp_arg("0", "passive_cancel_after_ns") == 0 &&
        llt::parse_nonnegative_timestamp_arg("9223372036854775807", "passive_cancel_after_ns") ==
            9223372036854775807LL &&
        expect_invalid("") &&
        expect_invalid("-1") &&
        expect_invalid("12x") &&
        expect_invalid("9223372036854775808");
    if (!ok) {
        std::cerr << "config_parser_tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "config_parser_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
