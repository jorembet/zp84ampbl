cmake_minimum_required(VERSION 3.16)
project(zp84-dsp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(zphid STATIC src/hid/zp_hid.c)
target_include_directories(zphid PUBLIC src)

add_executable(zpsniff tools/zpsniff.c)
target_link_libraries(zpsniff PRIVATE zphid)

add_executable(zp84web src/web/zp84web.c)
target_link_libraries(zp84web PRIVATE zphid m)

enable_testing()
add_executable(zpmath_test tests/zpmath_test.c src/dsp/biquad.c src/dsp/design.c)
target_link_libraries(zpmath_test PRIVATE m)
add_test(NAME zpmath COMMAND zpmath_test)

add_executable(zpframe_test tests/zpframe_test.c)
target_link_libraries(zpframe_test PRIVATE zphid)
add_test(NAME zpframe COMMAND zpframe_test)

option(ZP84_GUI "Build the Xlib GUI" OFF)
if(ZP84_GUI)
    find_package(X11 REQUIRED)
    add_executable(zpgui_data_test tests/zpgui_data_test.c src/dsp/biquad.c src/dsp/design.c)
    target_link_libraries(zpgui_data_test PRIVATE zphid ${X11_LIBRARIES} m)
    target_include_directories(zpgui_data_test PRIVATE ${X11_INCLUDE_DIR})
    add_test(NAME zpgui_data COMMAND zpgui_data_test)
    add_executable(zp84gui
        src/gui/zp84gui.c
        src/dsp/biquad.c
        src/dsp/design.c
    )
    target_link_libraries(zp84gui PRIVATE zphid ${X11_LIBRARIES} m)
    target_include_directories(zp84gui PRIVATE ${X11_INCLUDE_DIR})
endif()
