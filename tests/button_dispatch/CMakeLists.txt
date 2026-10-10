find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(button_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/buttons.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/button_dispatch.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${button_source} ${CMAKE_CURRENT_BINARY_DIR}/button_dispatch.inc buttonAction
    DEPENDS ${button_source} ../support/extract_functions.py VERBATIM)
add_executable(ButtonDispatchTests button_dispatch_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/button_dispatch.inc)
set_target_properties(ButtonDispatchTests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(ButtonDispatchTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge)
target_compile_definitions(ButtonDispatchTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_link_libraries(ButtonDispatchTests PRIVATE CppUTest)
add_test(NAME ButtonDispatchTests COMMAND ButtonDispatchTests)

set(matrix_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/matrix/matrix_driver.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pad_dispatch.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${matrix_source} ${CMAKE_CURRENT_BINARY_DIR}/pad_dispatch.inc MatrixDriver::padAction MatrixDriver::isPadPressed
    DEPENDS ${matrix_source} ../support/extract_functions.py VERBATIM)
target_sources(ButtonDispatchTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/pad_dispatch.inc)
