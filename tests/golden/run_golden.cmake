if(NOT DEFINED TRADING_MAIN OR NOT DEFINED FIXTURE OR NOT DEFINED GOLDEN OR
   NOT DEFINED WORK_DIR OR NOT DEFINED STYLE OR NOT DEFINED CANCEL_AFTER)
    message(FATAL_ERROR "golden runner is missing a required -D argument")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

function(run_replay mode suffix output_var journal_var)
    set(journal "${WORK_DIR}/${suffix}.journal.csv")
    execute_process(
        COMMAND "${TRADING_MAIN}" "${FIXTURE}" "${mode}" "${journal}" "${STYLE}" "${CANCEL_AFTER}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${mode} replay failed (${result}): ${stderr}\n${stdout}")
    endif()
    if(mode STREQUAL "async" AND NOT stdout MATCHES "dropped_async_events=0")
        message(FATAL_ERROR "async replay did not prove zero drops:\n${stdout}")
    endif()
    set(${output_var} "${stdout}" PARENT_SCOPE)
    set(${journal_var} "${journal}" PARENT_SCOPE)
endfunction()

function(normalize stdout journal output)
    file(READ "${journal}" journal_text)
    string(REPLACE "\r\n" "\n" journal_text "${journal_text}")
    string(REPLACE "\r" "\n" journal_text "${journal_text}")
    string(REPLACE "\r\n" "\n" stdout "${stdout}")
    string(REPLACE "\r" "\n" stdout "${stdout}")
    string(REPLACE "\n" ";" lines "${stdout}")
    set(normalized "# journal\n${journal_text}# summary\n")
    foreach(line IN LISTS lines)
        if(line MATCHES "^(execution_mode|passive_cancel_after_ns|orders|rejects|fills|persisted_events|dropped_async_events|run_status|venue_events\\..*|accounting\\..*|net_qty|avg_price|realized_pnl|unrealized_pnl|cash|order_manager\\..*)=")
            string(APPEND normalized "${line}\n")
        endif()
    endforeach()
    file(WRITE "${output}" "${normalized}")
endfunction()

run_replay(sync run1 stdout1 journal1)
run_replay(sync run2 stdout2 journal2)
normalize("${stdout1}" "${journal1}" "${WORK_DIR}/run1.normalized.txt")
normalize("${stdout2}" "${journal2}" "${WORK_DIR}/run2.normalized.txt")

execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
    "${WORK_DIR}/run1.normalized.txt" "${WORK_DIR}/run2.normalized.txt"
    RESULT_VARIABLE repeated_diff)
if(NOT repeated_diff EQUAL 0)
    message(FATAL_ERROR "repeated sync replay differs for ${FIXTURE}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
    "${WORK_DIR}/run1.normalized.txt" "${GOLDEN}"
    RESULT_VARIABLE golden_diff)
if(NOT golden_diff EQUAL 0)
    message(FATAL_ERROR "normalized replay differs from golden ${GOLDEN}")
endif()

run_replay(async async stdout_async journal_async)
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${journal1}" "${journal_async}"
    RESULT_VARIABLE async_diff)
if(NOT async_diff EQUAL 0)
    message(FATAL_ERROR "zero-drop async journal differs from sync journal for ${FIXTURE}")
endif()
