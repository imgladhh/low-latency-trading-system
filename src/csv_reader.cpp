#include "csv_reader.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace llt {

namespace {

template <typename T>
bool parse_integer(std::string_view token, T& value) {
    const char* begin = token.data();
    const char* end = token.data() + token.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    return ec == std::errc{} && ptr == end;
}

std::string make_error_message(
    const CsvErrorCategory category,
    const std::string& path,
    const std::size_t line,
    const std::string& detail) {
    std::ostringstream message;
    message << csv_error_category_name(category) << ": " << path;
    if (line != 0) {
        message << ':' << line;
    }
    message << ": " << detail;
    return message.str();
}

[[noreturn]] void throw_csv_error(
    const CsvErrorCategory category,
    const std::string& path,
    const std::size_t line,
    const char* detail) {
    throw CsvReadError(category, path, line, detail);
}

}  // namespace

const char* csv_error_category_name(const CsvErrorCategory category) noexcept {
    switch (category) {
    case CsvErrorCategory::IoError:
        return "IoError";
    case CsvErrorCategory::SchemaError:
        return "SchemaError";
    case CsvErrorCategory::ValueError:
        return "ValueError";
    case CsvErrorCategory::OrderingError:
        return "OrderingError";
    case CsvErrorCategory::MultiSymbolError:
        return "MultiSymbolError";
    }
    return "UnknownCsvError";
}

CsvReadError::CsvReadError(
    const CsvErrorCategory category,
    std::string path,
    const std::size_t line,
    std::string detail)
    : std::runtime_error(make_error_message(category, path, line, detail)),
      category_(category),
      path_(std::move(path)),
      line_(line) {}

std::vector<MarketTick> CsvReader::read_all(const char* path) const {
    const std::string input_path = path == nullptr ? "<null>" : path;
    if (path == nullptr || *path == '\0') {
        throw_csv_error(CsvErrorCategory::IoError, input_path, 0, "input path is empty");
    }

    std::ifstream input(path, std::ios::binary);
    std::vector<MarketTick> ticks;
    if (!input.is_open()) {
        throw_csv_error(CsvErrorCategory::IoError, input_path, 0, "cannot open input file");
    }

    std::string line;
    if (!std::getline(input, line)) {
        if (input.bad()) {
            throw_csv_error(CsvErrorCategory::IoError, input_path, 1, "failed while reading header");
        }
        throw_csv_error(CsvErrorCategory::SchemaError, input_path, 1, "missing header");
    }

    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        MarketTick tick{};
        parse_line(line.data(), line.data() + line.size(), input_path, line_number, tick);

        if (tick.bid_price <= 0 || tick.ask_price <= 0) {
            throw_csv_error(CsvErrorCategory::ValueError, input_path, line_number, "prices must be positive");
        }
        if (tick.bid_qty < 0 || tick.ask_qty < 0) {
            throw_csv_error(CsvErrorCategory::ValueError, input_path, line_number, "displayed quantities must be non-negative");
        }
        if (tick.bid_price > tick.ask_price) {
            throw_csv_error(CsvErrorCategory::ValueError, input_path, line_number, "bid price exceeds ask price");
        }
        if (tick.receive_ts_ns < tick.exchange_ts_ns) {
            throw_csv_error(CsvErrorCategory::ValueError, input_path, line_number, "receive timestamp precedes exchange timestamp");
        }
        if (!ticks.empty() && tick.receive_ts_ns < ticks.back().receive_ts_ns) {
            throw_csv_error(CsvErrorCategory::OrderingError, input_path, line_number, "receive timestamps are not nondecreasing");
        }
        if (!ticks.empty() && tick.symbol_id != ticks.front().symbol_id) {
            throw_csv_error(CsvErrorCategory::MultiSymbolError, input_path, line_number, "multiple symbols are not supported");
        }
        ticks.push_back(tick);
    }

    if (input.bad()) {
        throw_csv_error(CsvErrorCategory::IoError, input_path, line_number + 1, "failed while reading data");
    }
    if (ticks.empty()) {
        throw_csv_error(CsvErrorCategory::SchemaError, input_path, 1, "header-only input has no data rows");
    }

    return ticks;
}

void CsvReader::parse_line(
    const char* begin,
    const char* end,
    const std::string& path,
    const std::size_t line_number,
    MarketTick& tick) {
    std::string_view fields[7];
    std::size_t field_index = 0;
    const char* field_begin = begin;

    for (const char* cursor = begin; cursor <= end; ++cursor) {
        if (cursor == end || *cursor == ',') {
            if (field_index >= 7) {
                throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "expected exactly seven fields");
            }
            fields[field_index++] = std::string_view(field_begin, static_cast<std::size_t>(cursor - field_begin));
            field_begin = cursor + 1;
        }
    }

    if (field_index != 7) {
        throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "expected exactly seven fields");
    }

    for (const std::string_view field : fields) {
        if (field.empty()) {
            throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "required field is empty");
        }
    }

    if (!parse_integer(fields[0], tick.exchange_ts_ns)) {
        throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "exchange_ts_ns is not a complete integer");
    }
    if (!parse_integer(fields[1], tick.receive_ts_ns)) {
        throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "receive_ts_ns is not a complete integer");
    }
    if (!parse_integer(fields[2], tick.symbol_id)) {
        throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "symbol_id is not a complete integer");
    }
    if (!parse_integer(fields[3], tick.bid_price)) {
        throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "bid_price is not a complete integer");
    }
    if (!parse_integer(fields[4], tick.ask_price)) {
        throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "ask_price is not a complete integer");
    }
    if (!parse_integer(fields[5], tick.bid_qty)) {
        throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "bid_qty is not a complete integer");
    }
    if (!parse_integer(fields[6], tick.ask_qty)) {
        throw_csv_error(CsvErrorCategory::SchemaError, path, line_number, "ask_qty is not a complete integer");
    }
}

}  // namespace llt
