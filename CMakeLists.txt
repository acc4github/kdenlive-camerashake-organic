cmake_minimum_required(VERSION 3.10)
project(camerashakeorganic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREI0R REQUIRED frei0r)

add_library(camerashakeorganic SHARED camerashakeorganic.cpp)

set_target_properties(camerashakeorganic PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
    OUTPUT_NAME "camerashakeorganic"
)

target_include_directories(camerashakeorganic PRIVATE ${FREI0R_INCLUDE_DIRS})
target_link_libraries(camerashakeorganic PRIVATE ${FREI0R_LIBRARIES})
