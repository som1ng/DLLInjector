cmake_minimum_required(VERSION 3.21)
project(DLLInjector LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "DLLInjector currently requires a Windows toolchain.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

add_library(dllinjector_core STATIC
    core/common/result.cpp core/common/text.cpp core/process/process.cpp)
target_include_directories(dllinjector_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_compile_definitions(dllinjector_core PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0600)

add_library(cli_support STATIC apps/cli/parser.cpp apps/cli/report.cpp)
target_link_libraries(cli_support PUBLIC dllinjector_core)
target_include_directories(cli_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/apps/cli")
add_executable(dllinjector_cli apps/cli/main.cpp)
target_link_libraries(dllinjector_cli PRIVATE cli_support)
target_include_directories(dllinjector_cli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_compile_definitions(dllinjector_cli PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0600)
# Windows filenames are case-insensitive: preserve the legacy GUI path.
set_target_properties(dllinjector_cli PROPERTIES OUTPUT_NAME dllinjector
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/cli")

add_library(injector_utils STATIC src/utils.cpp)
target_include_directories(injector_utils PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_compile_definitions(injector_utils PUBLIC UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0600)
target_link_libraries(injector_utils PUBLIC dllinjector_core comdlg32 advapi32)

# Retain the legacy sources. The GNU x64 syscall experiment is deliberately
# outside this validated MSVC/MinGW GUI build; no assembly port is implied.
add_executable(DLLInjector WIN32
    src/main_gui.cpp
    src/methods/method_crt.cpp
    src/methods/method_apc.cpp
    src/methods/method_hook.cpp
    src/methods/method_reflective.cpp
    src/methods/method_hijack.cpp
)
target_compile_definitions(DLLInjector PRIVATE DLLINJECTOR_HAS_SYSCALL=0)
target_link_libraries(DLLInjector PRIVATE injector_utils user32 gdi32 comctl32 shlwapi)

add_executable(demo_target dll_test/demo.c)
add_library(demo_dll SHARED dll_test/dll_demo1.cpp)
target_link_libraries(demo_dll PRIVATE user32)

if(MSVC)
    foreach(target dllinjector_core cli_support dllinjector_cli injector_utils DLLInjector demo_target demo_dll)
        target_compile_options(${target} PRIVATE /utf-8)
    endforeach()
elseif(MINGW)
    target_link_options(dllinjector_cli PRIVATE -municode -static)
    target_link_options(DLLInjector PRIVATE -municode -static)
    target_link_options(demo_target PRIVATE -static)
    target_link_options(demo_dll PRIVATE -static)
endif()

if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_executable(core_tests tests/core_tests.cpp)
    target_include_directories(core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
    target_compile_definitions(core_tests PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0600)
    target_link_libraries(core_tests PRIVATE cli_support)
    add_executable(utils_tests tests/utils_tests.cpp)
    target_include_directories(utils_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
    target_link_libraries(utils_tests PRIVATE injector_utils)

    add_library(noop_fixture SHARED tests/fixtures/noop_plugin.cpp)
    add_executable(smoke_tests tests/smoke_tests.cpp)
    target_compile_definitions(smoke_tests PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0600)
    target_link_libraries(smoke_tests PRIVATE user32 advapi32)

    if(MSVC)
        foreach(target core_tests utils_tests noop_fixture smoke_tests)
            target_compile_options(${target} PRIVATE /utf-8 /W4)
        endforeach()
    elseif(MINGW)
        target_link_options(core_tests PRIVATE -static)
        target_link_options(utils_tests PRIVATE -static)
        target_link_options(noop_fixture PRIVATE -static)
        target_link_options(smoke_tests PRIVATE -municode -static)
    endif()

    foreach(case errors enumeration inspect timeout text-json)
        add_test(NAME core.${case} COMMAND core_tests "${case}")
        set_tests_properties(core.${case} PROPERTIES TIMEOUT 15)
    endforeach()
    add_test(NAME cli.contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_contract.py"
        "$<TARGET_FILE:dllinjector_cli>" "$<TARGET_FILE:demo_target>" "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(cli.contract PROPERTIES TIMEOUT 90)

    foreach(case picker architecture-modern architecture-errors architecture-legacy privileges live-process unicode-file)
        add_test(NAME utils.${case} COMMAND utils_tests "${case}")
        set_tests_properties(utils.${case} PROPERTIES TIMEOUT 15)
    endforeach()
    add_test(NAME smoke.gui COMMAND smoke_tests gui "$<TARGET_FILE:DLLInjector>")
    add_test(NAME smoke.demo COMMAND smoke_tests demo "$<TARGET_FILE:demo_target>")
    add_test(NAME smoke.fixture COMMAND smoke_tests fixture "$<TARGET_FILE:noop_fixture>")
    set_tests_properties(smoke.gui smoke.demo smoke.fixture PROPERTIES TIMEOUT 20)
endif()
