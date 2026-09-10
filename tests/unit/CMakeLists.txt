# CppUTest
include(FetchContent)
FetchContent_Declare(
        CppUTest
        GIT_REPOSITORY https://github.com/cpputest/cpputest.git
        GIT_TAG eccbc2190c672e598c1dd2bf5e4295f5ba27aad1
)
FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.39.4
)

FetchContent_MakeAvailable(etl)

# Set this to ON if you want to have the CppUTest's internal tests in your
# project as well.
set(TESTS OFF CACHE BOOL "Switch off CppUTest Test build")
if (WIN32)
    # Memory leak detection is off by default on Windows, setting
    # it here will cause a massive spam of redefinition warning during
    # build.
    #
    # We use things like strncat, which Windows wants to warn about: silence those.
    add_compile_definitions(
            IN_UNIT_TESTS=1
            _CRT_SECURE_NO_WARNINGS=1
    )
else ()
    add_compile_definitions(
            CPPUTEST_MEM_LEAK_DETECTION_DISABLED
            IN_UNIT_TESTS=1
    )
endif ()

FetchContent_MakeAvailable(CppUTest)

file(GLOB_RECURSE deluge_SOURCES
        # Mock implementations
        mocks/*
        # For LFO
        ../../src/deluge/util/d_stringbuf.cpp
        ../../src/deluge/util/lookuptables.cpp
        ../../src/deluge/util/waves.cpp
        ../../src/deluge/modulation/lfo.cpp
        ../../src/deluge/util/shift_value.cpp
        # For value scaling
        ../../src/deluge/gui/menu_item/value_scaling.cpp
        # For scale tests
        ../../src/deluge/model/scale/musical_key.cpp
        ../../src/deluge/model/scale/note_set.cpp
        ../../src/deluge/model/scale/preset_scales.cpp
        ../../src/deluge/model/scale/utils.cpp
        ../../src/deluge/model/scale/preset_scales.cpp
        ../../src/deluge/model/scale/scale_change.cpp
        ../../src/deluge/model/scale/scale_mapper.cpp
        # For clip iterator tests
        ../../src/deluge/model/song/clip_iterators.cpp
        # For sync tests
        ../../src/deluge/model/sync.cpp
        # For chord tests
        ../../src/deluge/gui/ui/keyboard/chords.cpp
        # For clock output scheduler tests
        ../../src/deluge/playback/clock_output_scheduler.cpp
        # For name comparison / default naming tests
        ../../src/deluge/util/name_compare.cpp
        ../../src/deluge/gui/ui/browser/default_name.cpp
)

add_executable(UnitTests
        RunAllTests.cpp
        scheduler_tests.cpp
        lfo_tests.cpp
        scale_tests.cpp
        value_scaling_tests.cpp
        clip_iterator_tests.cpp
        function_tests.cpp
        shift_value_tests.cpp
        param_value_binding_tests.cpp
        param_value_serializer_tests.cpp
        sync_tests.cpp
        chord_tests.cpp
        time_tests.cpp
        clock_output_scheduler_tests.cpp
        name_compare_tests.cpp
        default_name_tests.cpp
        rgb_tests.cpp
        notes_state_tests.cpp
        mirror_protocol_tests.cpp
        ui_session_tests.cpp
        favourites_session_tests.cpp
)
add_test(NAME UnitTests
        COMMAND UnitTests)
target_sources(UnitTests PRIVATE ${deluge_SOURCES})
target_include_directories(UnitTests PRIVATE
        # include the non test project source
        mocks
        ../../src
        ../../src/deluge
)

set_target_properties(UnitTests
        PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
)

target_link_libraries(UnitTests CppUTestExt etl::etl)

# strchr is seemingly different in x86
target_compile_options(UnitTests PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
)

# Compile the production Remote OLED snapshot accessor with real frame/session
# storage, without linking the hardware-backed OLED UI implementation.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(oled_snapshot_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/display/oled.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/oled_snapshot_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${oled_snapshot_source} ${CMAKE_CURRENT_BINARY_DIR}/oled_snapshot_method.inc OLED::copy_remote_frame OLED::invalidate_remote_frame
    DEPENDS ${oled_snapshot_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE oled_snapshot_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/oled_snapshot_method.inc)
target_include_directories(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set(seven_segment_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/display/seven_segment.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/seven_segment_render.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${seven_segment_source} ${CMAKE_CURRENT_BINARY_DIR}/seven_segment_render.inc SevenSegment::render
    DEPENDS ${seven_segment_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE seven_segment_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/seven_segment_render.inc)

set(pad_column_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/led/pad_leds.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pad_column_render.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${pad_column_source} ${CMAKE_CURRENT_BINARY_DIR}/pad_column_render.inc sortLedsForCol PadLEDs::flashMainPad
    DEPENDS ${pad_column_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE pad_column_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/pad_column_render.inc)
