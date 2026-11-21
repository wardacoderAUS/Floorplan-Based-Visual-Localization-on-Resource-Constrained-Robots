# GAP9 (GAP SDK):  cmake -B build && cmake --build build --target run
cmake_minimum_required(VERSION 3.19)
include($ENV{GAP_SDK_HOME}/utils/cmake/setup.cmake)
set(TARGET_NAME "pmu_demo")
project(${TARGET_NAME} C ASM)
add_executable(${TARGET_NAME} ../pmu.c ../workload.c ../mcu_main.c)   # or ../pmu.c ../demo.c
target_include_directories(${TARGET_NAME} PRIVATE ..)
setupos(${TARGET_NAME})
