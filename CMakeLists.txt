cmake_minimum_required(VERSION 3.16)
project(esp_system_native_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(esp_system_telemetry_core
    src/telemetry/NumericAggregate.cpp
)
target_include_directories(esp_system_telemetry_core PUBLIC src)
target_compile_options(esp_system_telemetry_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(telemetry_native_tests tests/native/test_telemetry.cpp)
target_link_libraries(telemetry_native_tests PRIVATE esp_system_telemetry_core)
target_compile_options(telemetry_native_tests PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME telemetry_native_tests COMMAND telemetry_native_tests)
