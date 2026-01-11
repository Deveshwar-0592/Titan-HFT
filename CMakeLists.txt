cmake_minimum_required(VERSION 3.10)
project(TitanHFT)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Compile Flags:
# -O3: Max optimization
# -march=native: Use instructions specific to this CPU (AVX2/AVX512)
# -pthread: Enable threading
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -pthread -Wall")
include_directories(include)

add_executable(titan_engine main.cpp)