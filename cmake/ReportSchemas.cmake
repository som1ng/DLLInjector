# Header-only parser/validator; downloads and generated schemas stay in the build tree.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(JSONCONS_BUILD_TESTS OFF CACHE BOOL "Build upstream jsoncons tests" FORCE)
FetchContent_Declare(jsoncons
    URL https://github.com/danielaparker/jsoncons/archive/refs/tags/v1.10.0.tar.gz
    URL_HASH SHA256=c829350a2eece662beb378143e2ab1c1cbb07d703687bf168626665a8a7d8bc1)
FetchContent_MakeAvailable(jsoncons)

set(report_schema_header "#pragma once\n#include <string_view>\nnamespace dllinjector::cli {\n")
foreach(minor RANGE 0 6)
    if(minor EQUAL 0)
        set(report_schema_file "${CMAKE_CURRENT_SOURCE_DIR}/schemas/cli-v1.json")
    else()
        set(report_schema_file "${CMAKE_CURRENT_SOURCE_DIR}/schemas/cli-v1.${minor}.json")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${report_schema_file}")
    file(READ "${report_schema_file}" report_schema_text)
    string(APPEND report_schema_header "inline constexpr std::string_view ReportSchema${minor} =\n")
    # MSVC limits individual string literals; concatenate bounded raw chunks.
    string(LENGTH "${report_schema_text}" report_schema_length)
    set(report_schema_offset 0)
    while(report_schema_offset LESS report_schema_length)
        string(SUBSTRING "${report_schema_text}" ${report_schema_offset} 8192 report_schema_chunk)
        string(APPEND report_schema_header "R\"schema(${report_schema_chunk})schema\"\n")
        math(EXPR report_schema_offset "${report_schema_offset} + 8192")
    endwhile()
    string(APPEND report_schema_header ";\n")
endforeach()
string(APPEND report_schema_header "}\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/report_schemas.h"
    CONTENT "${report_schema_header}" @ONLY)
