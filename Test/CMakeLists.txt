# Standalone serializer conformance — corpus byte-exactness + UE frame
# conversion + deriver checks. No engine / UEVR required; guards the
# vendored core (OgsTelemetry/*.h) against the standard's corpus:
#   cmake -S Test -B Test/build && cmake --build Test/build
#   Test/build/ogs_conformance ../../OGS-Telemetry-Standard/conformance
cmake_minimum_required(VERSION 3.16)
project(ogs_conformance CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(ogs_conformance conformance.cpp)
target_include_directories(ogs_conformance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
