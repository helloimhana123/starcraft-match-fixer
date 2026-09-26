cmake_minimum_required(VERSION 3.20)
project(pluto_fastest_latency_fix LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This project targets the Windows StarCraft client.")
endif()

add_library(pluto_fastest_latency_fix SHARED
  src/plugin.cpp
)

target_compile_features(pluto_fastest_latency_fix PRIVATE cxx_std_17)
target_compile_definitions(pluto_fastest_latency_fix PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(pluto_fastest_latency_fix PROPERTIES
  OUTPUT_NAME "PlutoFastestLatencyFix"
  PREFIX ""
)

if(MSVC)
  target_compile_options(pluto_fastest_latency_fix PRIVATE /W4 /WX)
endif()
