add_executable(SourceMenuTests source_menu_tests.cpp)
set_target_properties(SourceMenuTests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(SourceMenuTests PRIVATE mocks ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge)
target_compile_definitions(SourceMenuTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_link_libraries(SourceMenuTests PRIVATE CppUTest)
add_test(NAME SourceMenuTests COMMAND SourceMenuTests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(song_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/song/song.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clip_membership.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${song_source} ${CMAKE_CURRENT_BINARY_DIR}/clip_membership.inc Song::contains_clip_for_undo
    DEPENDS ${song_source} ../support/extract_functions.py VERBATIM)
target_sources(SourceMenuTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/clip_membership.inc)
