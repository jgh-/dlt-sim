cmake_minimum_required(VERSION 3.5)
project(dlt-sim)
set(CMAKE_BUILD_TYPE Debug)

include_directories(include cpp_modules/include ${CMAKE_CURRENT_BINARY_DIR})
link_directories(cpp_modules/lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE -std=c++14 -O0 -g -Wall -Wpedantic -Werror")

file(GLOB SOURCES "src/sim/*.cc")
add_library(dlt-sim STATIC ${SOURCES})

add_executable(obelisk "src/skycoin/obelisk.cc")
target_link_libraries(obelisk dlt-sim "cryptopp" "ncurses")