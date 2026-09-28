cmake_minimum_required(VERSION 3.16)
project(zp84-dsp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(zphid STATIC src/hid/zp_hid.c)
target_include_directories(zphid PUBLIC src)

add_executable(zpsniff tools/zpsniff.c)
target_link_libraries(zpsniff PRIVATE zphid)

enable_testing()
add_executable(zpmath_test tests/zpmath_test.c src/dsp/biquad.c src/dsp/design.c)
target_link_libraries(zpmath_test PRIVATE m)
add_test(NAME zpmath COMMAND zpmath_test)

add_executable(zpframe_test tests/zpframe_test.c)
target_link_libraries(zpframe_test PRIVATE zphid)
add_test(NAME zpframe COMMAND zpframe_test)

option(ZP84_GUI "Build the Qt6 GUI" OFF)
if(ZP84_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
    set(CMAKE_AUTOMOC ON)
    add_executable(zp84gui
        src/gui/main.cpp
        src/gui/mainwindow.cpp
        src/gui/geeq.cpp
        src/dsp/biquad.c
        src/dsp/design.c
    )
    target_link_libraries(zp84gui PRIVATE Qt6::Core Qt6::Widgets zphid)
endif()
