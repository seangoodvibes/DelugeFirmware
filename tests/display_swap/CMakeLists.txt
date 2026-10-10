find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(display_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/display/display.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/display_swap.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${display_source} ${CMAKE_CURRENT_BINARY_DIR}/display_swap.inc swapDisplayType
    DEPENDS ${display_source} ../support/extract_functions.py VERBATIM)
add_executable(DisplaySwapTests display_swap_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/display_swap.inc)
set_target_properties(DisplaySwapTests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(DisplaySwapTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge)
target_compile_definitions(DisplaySwapTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_link_libraries(DisplaySwapTests PRIVATE CppUTest)
add_test(NAME DisplaySwapTests COMMAND DisplaySwapTests)
