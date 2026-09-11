cmake_minimum_required(VERSION 3.20)

project(low_latency_trading_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(trading_kernel
    src/accounting_engine.cpp
    src/csv_reader.cpp
    src/execution_simulator.cpp
    src/fill_logger.cpp
    src/latency_stats.cpp
    src/layout_utils.cpp
    src/mock_venue_gateway.cpp
    src/order_manager.cpp
    src/risk_engine.cpp
    src/strategy.cpp
)

target_include_directories(trading_kernel PUBLIC include)

add_executable(trading_main src/main.cpp)
target_link_libraries(trading_main PRIVATE trading_kernel)

add_executable(accounting_tests tests/accounting_tests.cpp)
target_link_libraries(accounting_tests PRIVATE trading_kernel)

add_executable(latency_stats_tests tests/latency_stats_tests.cpp)
target_link_libraries(latency_stats_tests PRIVATE trading_kernel)

add_executable(spsc_ring_buffer_tests tests/spsc_ring_buffer_tests.cpp)
target_link_libraries(spsc_ring_buffer_tests PRIVATE trading_kernel)

add_executable(fill_logger_tests tests/fill_logger_tests.cpp)
target_link_libraries(fill_logger_tests PRIVATE trading_kernel)

add_executable(layout_tests tests/layout_tests.cpp)
target_link_libraries(layout_tests PRIVATE trading_kernel)

add_executable(execution_simulator_tests tests/execution_simulator_tests.cpp)
target_link_libraries(execution_simulator_tests PRIVATE trading_kernel)

add_executable(order_manager_tests tests/order_manager_tests.cpp)
target_link_libraries(order_manager_tests PRIVATE trading_kernel)

add_executable(mock_venue_gateway_tests tests/mock_venue_gateway_tests.cpp)
target_link_libraries(mock_venue_gateway_tests PRIVATE trading_kernel)

add_executable(csv_reader_tests tests/csv_reader_tests.cpp)
target_link_libraries(csv_reader_tests PRIVATE trading_kernel)

enable_testing()
add_test(NAME accounting_tests COMMAND accounting_tests)
add_test(NAME latency_stats_tests COMMAND latency_stats_tests)
add_test(NAME spsc_ring_buffer_tests COMMAND spsc_ring_buffer_tests)
add_test(NAME fill_logger_tests COMMAND fill_logger_tests)
add_test(NAME layout_tests COMMAND layout_tests)
add_test(NAME execution_simulator_tests COMMAND execution_simulator_tests)
add_test(NAME order_manager_tests COMMAND order_manager_tests)
add_test(NAME mock_venue_gateway_tests COMMAND mock_venue_gateway_tests)
add_test(NAME csv_reader_tests COMMAND csv_reader_tests)
