# Standalone serializer conformance — corpus byte-exactness + UE frame
# conversion + deriver checks. No engine / UEVR required; guards the
# vendored core (OgsaTelemetry/*.h) against the standard's corpus:
#   cmake -S Test -B Test/build && cmake --build Test/build
#   Test/build/ogsa_conformance ../../OGSA-Telemetry-Standard/conformance
cmake_minimum_required(VERSION 3.16)
project(ogsa_conformance CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(ogsa_conformance conformance.cpp)
target_include_directories(ogsa_conformance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
