find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(clip_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/clip/instrument_clip.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/instrument_clone_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${clip_source} ${CMAKE_CURRENT_BINARY_DIR}/instrument_clone_method.inc InstrumentClip::clone
    DEPENDS ${clip_source} ../support/extract_functions.py VERBATIM)
add_executable(InstrumentCloneTests ../unit/RunAllTests.cpp instrument_clone_tests.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/instrument_clone_method.inc)
target_include_directories(InstrumentCloneTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(InstrumentCloneTests PRIVATE CppUTestExt)
target_compile_features(InstrumentCloneTests PRIVATE cxx_std_23)
target_compile_definitions(InstrumentCloneTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
if(UNDO_TEST_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(InstrumentCloneTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(InstrumentCloneTests PRIVATE -fsanitize=address,undefined)
endif()
add_dependencies(UnitTests InstrumentCloneTests)
add_test(NAME InstrumentCloneTests COMMAND InstrumentCloneTests)
