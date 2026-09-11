#include <cstdlib>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

#include "fill_logger.h"

namespace {

bool check_eq(const char* label, const std::string& actual, const std::string& expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected='" << expected << "' actual='" << actual << "'\n";
    return false;
}

bool test_fill_log_format() {
    std::ostringstream out;
    llt::write_trade_event_log(out, llt::TradeEvent{
        1010, 99900, 50, llt::EventKind::Fill, llt::Side::Buy, llt::RejectReason::None});
    return check_eq("fill_log", out.str(), "fill ts_ns=1010 side=BUY price=99900 qty=50\n");
}

bool test_reject_log_format() {
    std::ostringstream out;
    llt::write_trade_event_log(out, llt::TradeEvent{
        2020, 0, 75, llt::EventKind::RiskReject, llt::Side::Sell, llt::RejectReason::MaxPosition});
    return check_eq("reject_log", out.str(), "reject ts_ns=2020 side=SELL qty=75 reason=MAX_POSITION\n");
}

bool test_persistence_format() {
    std::ostringstream out;
    return llt::write_trade_event_persistence(out, llt::TradeEvent{
               3030, 0, 25, llt::EventKind::RiskReject, llt::Side::Buy, llt::RejectReason::MaxPosition}) &&
        check_eq("persist_log", out.str(), "RISK_REJECT,3030,BUY,0,25,MAX_POSITION\n");
}

class FailingBuffer final : public std::streambuf {
protected:
    int_type overflow(int_type) override { return traits_type::eof(); }
    std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};

bool test_persistence_failure() {
    FailingBuffer buffer;
    std::ostream out(&buffer);
    const bool persisted = llt::write_trade_event_persistence(out, llt::TradeEvent{
        4040, 100000, 10, llt::EventKind::Fill, llt::Side::Buy, llt::RejectReason::None});
    return !persisted && !out.good();
}

}  // namespace

int main() {
    const bool ok = test_fill_log_format() &&
        test_reject_log_format() &&
        test_persistence_format() &&
        test_persistence_failure();
    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "fill_logger_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
