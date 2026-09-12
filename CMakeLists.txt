cmake_minimum_required(VERSION 3.20)

project(low_latency_trading_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(trading_kernel
    src/accounting_engine.cpp
    src/config_parser.cpp
    src/csv_reader.cpp
    src/execution_simulator.cpp
    src/fill_logger.cpp
    src/latency_stats.cpp
    src/layout_utils.cpp
    src/mock_venue_gateway.cpp
    src/order_manager.cpp
    src/replay_runner.cpp
    src/risk_engine.cpp
    src/strategy.cpp
)

target_include_directories(trading_kernel PUBLIC include)

add_executable(trading_main src/main.cpp)
target_link_libraries(trading_main PRIVATE trading_kernel)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE LLT_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT LLT_GIT_COMMIT)
    set(LLT_GIT_COMMIT unknown)
endif()

add_executable(generate_replay bench/generate_replay.cpp)
add_executable(bench_replay bench/bench_replay.cpp)
target_link_libraries(bench_replay PRIVATE trading_kernel)
target_compile_definitions(bench_replay PRIVATE
    LLT_BUILD_TYPE="$<CONFIG>"
    LLT_GIT_COMMIT="${LLT_GIT_COMMIT}"
)

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

add_executable(config_parser_tests tests/config_parser_tests.cpp)
target_link_libraries(config_parser_tests PRIVATE trading_kernel)

add_executable(risk_engine_tests tests/risk_engine_tests.cpp)
target_link_libraries(risk_engine_tests PRIVATE trading_kernel)

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
add_test(NAME config_parser_tests COMMAND config_parser_tests)
add_test(NAME risk_engine_tests COMMAND risk_engine_tests)

set(DETERMINISM_CASES
    "aggressive_full|aggressive|3000"
    "aggressive_partial_expired|aggressive|3000"
    "passive_partial|passive|10000"
    "cancel_ack|passive|1000"
    "venue_reject|aggressive|3000"
    "risk_reject|aggressive|3000"
)
foreach(case IN LISTS DETERMINISM_CASES)
    string(REPLACE "|" ";" case_fields "${case}")
    list(GET case_fields 0 case_name)
    list(GET case_fields 1 case_style)
    list(GET case_fields 2 case_cancel_after)
    add_test(
        NAME deterministic_replay_${case_name}
        COMMAND ${CMAKE_COMMAND}
            -DTRADING_MAIN=$<TARGET_FILE:trading_main>
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/determinism/${case_name}.csv
            -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/${case_name}.txt
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/golden/${case_name}
            -DSTYLE=${case_style}
            -DCANCEL_AFTER=${case_cancel_after}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/run_golden.cmake
    )
endforeach()
