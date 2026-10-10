find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(context_menu_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/context_menu/context_menu.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/context_menu_input.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${context_menu_source} ${CMAKE_CURRENT_BINARY_DIR}/context_menu_input.inc
        ContextMenu::selectEncoderAction ContextMenu::drawCurrentOption
    DEPENDS ${context_menu_source} ../support/extract_functions.py VERBATIM)
add_executable(ContextMenuTests launch_style_tests.cpp audio_input_tests.cpp clip_settings_tests.cpp
    ../../src/deluge/gui/context_menu/clip_settings/clip_settings.cpp
    ../../src/deluge/gui/context_menu/audio_input_selector.cpp ${CMAKE_CURRENT_BINARY_DIR}/context_menu_input.inc
    ../../src/deluge/gui/context_menu/clip_settings/launch_style.cpp)
set_target_properties(ContextMenuTests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(ContextMenuTests PRIVATE mocks ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge)
target_compile_definitions(ContextMenuTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_link_libraries(ContextMenuTests PRIVATE CppUTest)
add_test(NAME ContextMenuTests COMMAND ContextMenuTests)

set(song_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/song/song.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clip_membership.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${song_source} ${CMAKE_CURRENT_BINARY_DIR}/clip_membership.inc Song::contains_clip_for_undo
    DEPENDS ${song_source} ../support/extract_functions.py VERBATIM)
target_sources(ContextMenuTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/clip_membership.inc)
