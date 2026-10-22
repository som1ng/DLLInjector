cmake_minimum_required(VERSION 3.21)
project(DLLInjector LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "DLLInjector currently requires a Windows toolchain.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

add_library(injector_utils STATIC src/utils.cpp)
target_include_directories(injector_utils PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(injector_utils PUBLIC UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0600)
target_link_libraries(injector_utils PUBLIC comdlg32 advapi32)

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
    foreach(target injector_utils DLLInjector demo_target demo_dll)
        target_compile_options(${target} PRIVATE /utf-8)
    endforeach()
elseif(MINGW)
    target_link_options(DLLInjector PRIVATE -municode -static)
    target_link_options(demo_target PRIVATE -static)
    target_link_options(demo_dll PRIVATE -static)
endif()

if(BUILD_TESTING)
    add_executable(utils_tests tests/utils_tests.cpp)
    target_link_libraries(utils_tests PRIVATE injector_utils)

    add_library(noop_fixture SHARED tests/fixtures/noop_plugin.cpp)
    add_executable(smoke_tests tests/smoke_tests.cpp)
    target_compile_definitions(smoke_tests PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0600)
    target_link_libraries(smoke_tests PRIVATE user32 advapi32)

    if(MSVC)
        foreach(target utils_tests noop_fixture smoke_tests)
            target_compile_options(${target} PRIVATE /utf-8 /W4)
        endforeach()
    elseif(MINGW)
        target_link_options(utils_tests PRIVATE -static)
        target_link_options(noop_fixture PRIVATE -static)
        target_link_options(smoke_tests PRIVATE -municode -static)
    endif()

    foreach(case picker architecture-modern architecture-errors architecture-legacy privileges live-process unicode-file)
        add_test(NAME utils.${case} COMMAND utils_tests "${case}")
        set_tests_properties(utils.${case} PROPERTIES TIMEOUT 15)
    endforeach()
    add_test(NAME smoke.gui COMMAND smoke_tests gui "$<TARGET_FILE:DLLInjector>")
    add_test(NAME smoke.demo COMMAND smoke_tests demo "$<TARGET_FILE:demo_target>")
    add_test(NAME smoke.fixture COMMAND smoke_tests fixture "$<TARGET_FILE:noop_fixture>")
    set_tests_properties(smoke.gui smoke.demo smoke.fixture PROPERTIES TIMEOUT 20)
endif()
