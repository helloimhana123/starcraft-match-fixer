cmake_minimum_required(VERSION 3.20)
project(matchfixer LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This project targets the Windows StarCraft client.")
endif()

add_library(matchfixer SHARED
  src/plugin.cpp
)

target_compile_features(matchfixer PRIVATE cxx_std_17)
target_compile_definitions(matchfixer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(matchfixer PROPERTIES
  OUTPUT_NAME "MatchFixer"
  PREFIX ""
)

if(MSVC)
  target_compile_options(matchfixer PRIVATE /W4 /WX)
endif()
