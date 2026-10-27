cmake_minimum_required(VERSION 3.16)
project(ogs_ts_uevr CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# UEVR plugin DLL (any DLL exporting uevr_plugin_initialize loads; the
# loader iterates the plugins directory)
add_library(ogs_telemetry SHARED
    plugin/Plugin.cpp
    plugin/ogs_publisher.cpp)
target_include_directories(ogs_telemetry PRIVATE vendor plugin .)
target_link_libraries(ogs_telemetry ws2_32)

# Standalone smoke test — same publisher code, synthetic pawn motion
add_executable(ogs_smoke Test/smoke.cpp plugin/ogs_publisher.cpp)
target_include_directories(ogs_smoke PRIVATE vendor plugin .)
target_link_libraries(ogs_smoke ws2_32)
