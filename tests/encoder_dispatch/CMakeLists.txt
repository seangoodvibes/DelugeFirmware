find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(encoder_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/encoder_input.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/encoder_dispatch.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${encoder_source} ${CMAKE_CURRENT_BINARY_DIR}/encoder_dispatch.inc interpret_encoder_bank
    DEPENDS ${encoder_source} ../support/extract_functions.py VERBATIM)
add_executable(EncoderDispatchTests encoder_dispatch_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/encoder_dispatch.inc)
set_target_properties(EncoderDispatchTests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(EncoderDispatchTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge ../../src)
target_compile_definitions(EncoderDispatchTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_link_libraries(EncoderDispatchTests PRIVATE CppUTest)
add_test(NAME EncoderDispatchTests COMMAND EncoderDispatchTests)
