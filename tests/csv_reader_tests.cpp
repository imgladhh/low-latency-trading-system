#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "csv_reader.h"

namespace {

class FixtureDirectory {
public:
    ~FixtureDirectory() {
        for (const std::string& path : paths_) {
            (void) std::remove(path.c_str());
        }
    }

    std::string write(const char* name, const std::string& contents) {
        const std::string path = std::string("csv_reader_test_") + name;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        paths_.push_back(path);
        return path;
    }

private:
    std::vector<std::string> paths_;
};

bool check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << label << " failed\n";
    }
    return condition;
}

bool expect_error(
    const std::string& path,
    const llt::CsvErrorCategory category,
    const std::size_t line,
    const char* expected_detail = nullptr) {
    try {
        (void) llt::CsvReader{}.read_all(path.c_str());
    } catch (const llt::CsvReadError& error) {
        return check(error.category() == category, "error category") &&
            check(error.line() == line, "error line") &&
            check(error.path() == path, "error path") &&
            check(std::string(error.what()).find(llt::csv_error_category_name(category)) != std::string::npos,
                "diagnostic category") &&
            check(expected_detail == nullptr ||
                std::string(error.what()).find(expected_detail) != std::string::npos,
                "diagnostic detail");
    }
    std::cerr << "expected CsvReadError\n";
    return false;
}

bool test_valid_lf_and_crlf(FixtureDirectory& fixtures) {
    const std::string header = "exchange_ts_ns,receive_ts_ns,symbol_id,bid_price,ask_price,bid_qty,ask_qty";
    const auto lf = fixtures.write("valid_lf.csv",
        header + "\n100,110,1,10000,10001,5,6\n110,120,1,10001,10002,7,8\n");
    const auto crlf = fixtures.write("valid_crlf.csv",
        header + "\r\n100,110,1,10000,10001,5,6\r\n110,120,1,10001,10002,7,8\r\n");
    const auto lf_ticks = llt::CsvReader{}.read_all(lf.c_str());
    const auto crlf_ticks = llt::CsvReader{}.read_all(crlf.c_str());
    return check(lf_ticks.size() == 2, "LF row count") &&
        check(crlf_ticks.size() == 2, "CRLF row count") &&
        check(crlf_ticks[1].ask_qty == 8, "CRLF final field");
}

bool test_io_and_empty_inputs(FixtureDirectory& fixtures) {
    const auto missing = fixtures.write("temporary.csv", "unused");
    (void) std::remove(missing.c_str());
    const auto empty = fixtures.write("empty.csv", "");
    const auto header_only = fixtures.write("header_only.csv",
        "exchange_ts_ns,receive_ts_ns,symbol_id,bid_price,ask_price,bid_qty,ask_qty\n");
    return expect_error(missing, llt::CsvErrorCategory::IoError, 0) &&
        expect_error(empty, llt::CsvErrorCategory::SchemaError, 1, "missing header") &&
        expect_error(header_only, llt::CsvErrorCategory::SchemaError, 1, "header-only");
}

bool test_schema_errors(FixtureDirectory& fixtures) {
    const std::string header = "h\n";
    return expect_error(fixtures.write("missing.csv", header + "100,110,1,10000,10001,5\n"),
            llt::CsvErrorCategory::SchemaError, 2) &&
        expect_error(fixtures.write("extra.csv", header + "100,110,1,10000,10001,5,6,7\n"),
            llt::CsvErrorCategory::SchemaError, 2) &&
        expect_error(fixtures.write("empty_field.csv", header + "100,110,1,,10001,5,6\n"),
            llt::CsvErrorCategory::SchemaError, 2) &&
        expect_error(fixtures.write("nonnumeric.csv", header + "100,110x,1,10000,10001,5,6\n"),
            llt::CsvErrorCategory::SchemaError, 2, "receive_ts_ns");
}

bool test_value_errors(FixtureDirectory& fixtures) {
    const std::string header = "h\n";
    return expect_error(fixtures.write("crossed.csv", header + "100,110,1,10002,10001,5,6\n"),
            llt::CsvErrorCategory::ValueError, 2) &&
        expect_error(fixtures.write("negative_qty.csv", header + "100,110,1,10000,10001,-1,6\n"),
            llt::CsvErrorCategory::ValueError, 2) &&
        expect_error(fixtures.write("nonpositive_price.csv", header + "100,110,1,0,10001,5,6\n"),
            llt::CsvErrorCategory::ValueError, 2) &&
        expect_error(fixtures.write("receive_before_exchange.csv", header + "110,100,1,10000,10001,5,6\n"),
            llt::CsvErrorCategory::ValueError, 2);
}

bool test_cross_row_errors_and_fail_fast(FixtureDirectory& fixtures) {
    const std::string header = "h\n";
    return expect_error(fixtures.write("ordering.csv",
            header + "100,120,1,10000,10001,5,6\n100,119,1,10000,10001,5,6\n"),
            llt::CsvErrorCategory::OrderingError, 3) &&
        expect_error(fixtures.write("multi_symbol.csv",
            header + "100,110,1,10000,10001,5,6\n110,120,2,10000,10001,5,6\n"),
            llt::CsvErrorCategory::MultiSymbolError, 3) &&
        expect_error(fixtures.write("malformed_middle.csv",
            header + "100,110,1,10000,10001,5,6\n110,bad,1,10000,10001,5,6\n120,130,1,10000,10001,5,6\n"),
            llt::CsvErrorCategory::SchemaError, 3);
}

}  // namespace

int main() {
    FixtureDirectory fixtures;
    const bool ok = test_valid_lf_and_crlf(fixtures) &&
        test_io_and_empty_inputs(fixtures) &&
        test_schema_errors(fixtures) &&
        test_value_errors(fixtures) &&
        test_cross_row_errors_and_fail_fast(fixtures);
    if (!ok) {
        return EXIT_FAILURE;
    }
    std::cout << "csv_reader_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
