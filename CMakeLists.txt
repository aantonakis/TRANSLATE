cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS -pthread)

project(LArCADe C CXX)

set(CMAKE_CXX_STANDARD 17)

file(GLOB LArCADe_SRC
    "src/*.h"
    "src/*.cpp"
)

add_executable(LArCADe ${LArCADe_SRC})
