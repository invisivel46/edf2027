if(NOT EXISTS "${REQUIRED_FILE}")
    message(FATAL_ERROR
        "Required packaging tool not found: ${REQUIRED_FILE}\n"
        "Configure with -DEDF2017_TOOLS=<directory containing extract-xiso>.")
endif()
