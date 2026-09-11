#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "types.h"

namespace llt {

enum class CsvErrorCategory {
    IoError = 0,
    SchemaError = 1,
    ValueError = 2,
    OrderingError = 3,
    MultiSymbolError = 4,
};

[[nodiscard]] const char* csv_error_category_name(CsvErrorCategory category) noexcept;

class CsvReadError final : public std::runtime_error {
public:
    CsvReadError(
        CsvErrorCategory category,
        std::string path,
        std::size_t line,
        std::string detail);

    [[nodiscard]] CsvErrorCategory category() const noexcept { return category_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    CsvErrorCategory category_;
    std::string path_;
    std::size_t line_;
};

class CsvReader {
public:
    [[nodiscard]] std::vector<MarketTick> read_all(const char* path) const;

private:
    static void parse_line(
        const char* begin,
        const char* end,
        const std::string& path,
        std::size_t line_number,
        MarketTick& tick);
};

}  // namespace llt
