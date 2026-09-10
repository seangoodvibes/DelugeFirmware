find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(arrangement_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/playback/mode/arrangement.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/arrangement_clone_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${arrangement_source} ${CMAKE_CURRENT_BINARY_DIR}/arrangement_clone_method.inc Arrangement::doUniqueCloneOnClipInstance
    DEPENDS ${arrangement_source} ../support/extract_functions.py VERBATIM)
add_executable(ArrangementCloneTests ../unit/RunAllTests.cpp arrangement_clone_tests.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/arrangement_clone_method.inc)
target_include_directories(ArrangementCloneTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge)
target_link_libraries(ArrangementCloneTests PRIVATE CppUTestExt)
target_compile_features(ArrangementCloneTests PRIVATE cxx_std_23)
target_compile_definitions(ArrangementCloneTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
if(UNDO_TEST_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ArrangementCloneTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ArrangementCloneTests PRIVATE -fsanitize=address,undefined)
endif()
add_dependencies(UnitTests ArrangementCloneTests)
add_test(NAME ArrangementCloneTests COMMAND ArrangementCloneTests)
