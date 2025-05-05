cmake_minimum_required(VERSION 3.12)
project(tscns_lib)

file(GLOB HEADERS "*.h")
#file(GLOB SOURCES "*.cc")
#add_library(tscns_lib ${HEADERS} ${SOURCES})
add_library(tscns_lib ${HEADERS})
set_target_properties(tscns_lib PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(tscns_lib PUBLIC "..")