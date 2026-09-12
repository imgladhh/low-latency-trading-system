#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <limits>

namespace {

bool parse_positive_count(const std::string_view text, std::size_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    return value != 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: generate_replay <output.csv> <tick-count>\n";
        return EXIT_FAILURE;
    }
    std::size_t tick_count = 0;
    const std::string_view count_text(argv[2]);
    if (!parse_positive_count(count_text, tick_count)) {
        std::cerr << "tick-count must be a positive integer\n";
        return EXIT_FAILURE;
    }
    // Binary mode keeps the committed generated-data hash identical on Windows and Linux.
    std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "failed to open output: " << argv[1] << '\n';
        return EXIT_FAILURE;
    }
    out << "exchange_ts_ns,receive_ts_ns,symbol_id,bid_price,ask_price,bid_qty,ask_qty\n";
    std::uint64_t state = 0x4d595df4d0f33173ULL;
    for (std::size_t i = 0; i < tick_count; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::int64_t drift = static_cast<std::int64_t>((state >> 32) % 401) - 200;
        const std::int64_t bid = 99800 + drift;
        const std::int64_t ask = bid + 200;
        const std::int64_t ts = static_cast<std::int64_t>(i + 1) * 1000;
        out << ts << ',' << ts + 50 << ",1," << bid << ',' << ask << ",50,50\n";
    }
    if (!out) {
        std::cerr << "failed while writing output: " << argv[1] << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "generated_ticks=" << tick_count << " seed=0x4d595df4d0f33173 output=" << argv[1] << '\n';
    return EXIT_SUCCESS;
}
