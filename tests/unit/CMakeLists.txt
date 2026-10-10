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
        # For iterance migration tests (links against the real iterancePresets[]
        # from lookuptables.cpp, already included above)
        ../../src/deluge/model/iterance/iterance_migration.cpp
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
        encoder_acceleration_tests.cpp
        rgb_tests.cpp
        notes_state_tests.cpp
        iterance_migration_tests.cpp
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

set(session_startup_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/session_view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/session_startup_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${session_startup_source} ${CMAKE_CURRENT_BINARY_DIR}/session_startup_reset.inc SessionView::reset_for_remote_startup
    DEPENDS ${session_startup_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE session_startup_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/session_startup_reset.inc)

set(button_reset_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/buttons.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clear_shift_sticky.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${button_reset_source} ${CMAKE_CURRENT_BINARY_DIR}/clear_shift_sticky.inc clearShiftSticky reset_for_session_startup "bool isButtonPressed" "bool shiftHasChanged" "bool isShiftButtonPressed"
    DEPENDS ${button_reset_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE remote_shift_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/clear_shift_sticky.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/oled_working_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${oled_snapshot_source} ${CMAKE_CURRENT_BINARY_DIR}/oled_working_reset.inc OLED::removeWorkingAnimation OLED::clear_console_for_session
    DEPENDS ${oled_snapshot_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE oled_working_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/oled_working_reset.inc)

set(sound_editor_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/sound_editor.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/kit_shortcut_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${sound_editor_source} ${CMAKE_CURRENT_BINARY_DIR}/kit_shortcut_routing.inc SoundEditor::shouldEditKitAffectEntire
    DEPENDS ${sound_editor_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE kit_shortcut_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/kit_shortcut_routing.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/seven_segment_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${seven_segment_source} ${CMAKE_CURRENT_BINARY_DIR}/seven_segment_reset.inc SevenSegment::deleteAllLayers SevenSegment::reset_layers_for_session
    DEPENDS ${seven_segment_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE seven_segment_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/seven_segment_reset.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/oled_startup_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${oled_snapshot_source} ${CMAKE_CURRENT_BINARY_DIR}/oled_startup_reset.inc OLED::reset_layers_for_session OLED::clearMainImage OLED::stopBlink OLED::stopScrollingAnimation
    DEPENDS ${oled_snapshot_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE oled_startup_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/oled_startup_reset.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pad_startup_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${pad_column_source} ${CMAKE_CURRENT_BINARY_DIR}/pad_startup_reset.inc reset_transitions_for_session
    DEPENDS ${pad_column_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE pad_startup_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/pad_startup_reset.inc)

set(view_startup_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/midi_learn_startup_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${view_startup_source} ${CMAKE_CURRENT_BINARY_DIR}/midi_learn_startup_reset.inc View::reset_midi_learn_for_session_startup
    DEPENDS ${view_startup_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE midi_learn_startup_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/midi_learn_startup_reset.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/modulation_startup_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${view_startup_source} ${CMAKE_CURRENT_BINARY_DIR}/modulation_startup_reset.inc View::reset_modulation_for_session_startup
    DEPENDS ${view_startup_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE modulation_startup_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/modulation_startup_reset.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sound_editor_startup_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${sound_editor_source} ${CMAKE_CURRENT_BINARY_DIR}/sound_editor_startup_reset.inc SoundEditor::reset_for_session_startup
    DEPENDS ${sound_editor_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE sound_editor_startup_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/sound_editor_startup_reset.inc)

set(horizontal_menu_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/horizontal_menu.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/horizontal_menu_input.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${horizontal_menu_source} ${CMAKE_CURRENT_BINARY_DIR}/horizontal_menu_input.inc HorizontalMenu::buttonAction
    DEPENDS ${horizontal_menu_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE horizontal_menu_input_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/horizontal_menu_input.inc)

set(recording_scroll_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/clip_navigation_timeline_view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/recording_scroll_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${recording_scroll_source} ${CMAKE_CURRENT_BINARY_DIR}/recording_scroll_routing.inc ClipNavigationTimelineView::recording_scroll_for_session ClipNavigationTimelineView::focusRegained ClipNavigationTimelineView::horizontalEncoderAction
    DEPENDS ${recording_scroll_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE recording_scroll_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/recording_scroll_routing.inc)

set(decimal_cursor_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/decimal.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/decimal_cursor_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${decimal_cursor_source} ${CMAKE_CURRENT_BINARY_DIR}/decimal_cursor_routing.inc moving_cursor_for_session Decimal::horizontalEncoderAction
    DEPENDS ${decimal_cursor_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE decimal_cursor_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/decimal_cursor_routing.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/launch_playhead_buffer.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${session_startup_source} ${CMAKE_CURRENT_BINARY_DIR}/launch_playhead_buffer.inc SessionView::potentiallyRenderClipLaunchPlayhead
    DEPENDS ${session_startup_source} ../support/extract_functions.py VERBATIM)
set(keyboard_playhead_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/keyboard/keyboard_screen.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/keyboard_playhead_buffer.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${keyboard_playhead_source} ${CMAKE_CURRENT_BINARY_DIR}/keyboard_playhead_buffer.inc KeyboardScreen::graphicsRoutine
    DEPENDS ${keyboard_playhead_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE playhead_buffer_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/launch_playhead_buffer.inc ${CMAKE_CURRENT_BINARY_DIR}/keyboard_playhead_buffer.inc)

set(browser_catalog_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/browser/browser.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/browser_catalog_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${browser_catalog_source} ${CMAKE_CURRENT_BINARY_DIR}/browser_catalog_routing.inc
        Browser::session_state Browser::max_file_items_for_session Browser::catalog_search_direction_for_session
        Browser::sortFileItems Browser::cullSomeFileItems
    DEPENDS ${browser_catalog_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE browser_catalog_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/browser_catalog_routing.inc)

set(dx_cartridge_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/dx/cartridge.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dx_cartridge_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${dx_cartridge_source} ${CMAKE_CURRENT_BINARY_DIR}/dx_cartridge_routing.inc
        DxCartridge::state_for_session DxCartridge::tryLoad DxCartridge::selectEncoderAction
    DEPENDS ${dx_cartridge_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE dx_cartridge_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/dx_cartridge_routing.inc)

set(midi_device_menu_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/midi/devices.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/midi_device_menu_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${midi_device_menu_source} ${CMAKE_CURRENT_BINARY_DIR}/midi_device_menu_routing.inc
        Devices::scroll_for_session Devices::beginSession Devices::computeScrollForSelected
        Devices::selectEncoderAction Devices::drawPixelsForOled
    DEPENDS ${midi_device_menu_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE midi_device_menu_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/midi_device_menu_routing.inc)

set(multi_range_routing_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/multi_range.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/multi_range_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${multi_range_routing_source} ${CMAKE_CURRENT_BINARY_DIR}/multi_range_routing.inc
        MultiRange::destination_for_session MultiRange::scroll_for_session MultiRange::beginSession
        MultiRange::selectButtonPress MultiRange::noteOnToChangeRange
    DEPENDS ${multi_range_routing_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE multi_range_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/multi_range_routing.inc)

set(recorder_menu_routing_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/osc/audio_recorder.h)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/recorder_menu_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${recorder_menu_routing_source} ${CMAKE_CURRENT_BINARY_DIR}/recorder_menu_routing.inc
        AudioRecorder::destination_parent_for_session AudioRecorder::destination_for_session AudioRecorder::beginSession
    DEPENDS ${recorder_menu_routing_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE recorder_menu_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/recorder_menu_routing.inc)

set(patch_strength_state_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/patch_cable_strength.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/patch_strength_state.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${patch_strength_state_source} ${CMAKE_CURRENT_BINARY_DIR}/patch_strength_state.inc
        PatchCableStrength::scroll_delay_for_session PatchCableStrength::polarity_for_session
        PatchCableStrength::cable_exists_for_session PatchCableStrength::beginSession
        PatchCableStrength::horizontalEncoderAction PatchCableStrength::appendAdditionalDots
        PatchCableStrength::readCurrentValue PatchCableStrength::setPatchCablePolarity
    DEPENDS ${patch_strength_state_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE patch_strength_state_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/patch_strength_state.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/patch_strength_write.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${patch_strength_state_source} ${CMAKE_CURRENT_BINARY_DIR}/patch_strength_write.inc
        PatchCableStrength::writeCurrentValue
    DEPENDS ${patch_strength_state_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE patch_strength_write_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/patch_strength_write.inc)

set(patched_pan_read_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/patched_param/pan.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/patched_pan_read.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${patched_pan_read_source} ${CMAKE_CURRENT_BINARY_DIR}/patched_pan_read.inc Pan::readCurrentValue
    DEPENDS ${patched_pan_read_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/patched_pan_read.inc)

set(unpatched_pan_read_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/unpatched_param/pan.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/unpatched_pan_read.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${unpatched_pan_read_source} ${CMAKE_CURRENT_BINARY_DIR}/unpatched_pan_read.inc Pan::readCurrentValue
    DEPENDS ${unpatched_pan_read_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/unpatched_pan_read.inc)

set(patched_integer_read_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/patched_param/integer.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/patched_integer_read.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${patched_integer_read_source} ${CMAKE_CURRENT_BINARY_DIR}/patched_integer_read.inc Integer::readCurrentValue
    DEPENDS ${patched_integer_read_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/patched_integer_read.inc)

set(unpatched_standard_read_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/unpatched_param.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/unpatched_standard_read.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${unpatched_standard_read_source} ${CMAKE_CURRENT_BINARY_DIR}/unpatched_standard_read.inc UnpatchedParam::readCurrentValue
    DEPENDS ${unpatched_standard_read_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/unpatched_standard_read.inc)
target_sources(UnitTests PRIVATE parameter_read_routing_tests.cpp)

set(patch_indicator_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/patched_param.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/patch_indicator_routing.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${patch_indicator_source} ${CMAKE_CURRENT_BINARY_DIR}/patch_indicator_routing.inc
        PatchedParam::shouldDrawDotOnName PatchedParam::shouldBlinkPatchingSourceShortcut
    DEPENDS ${patch_indicator_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE patch_indicator_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/patch_indicator_routing.inc)

set(automation_selection_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/automation/automation.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/automation_selection_context.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${automation_selection_source} ${CMAKE_CURRENT_BINARY_DIR}/automation_selection_context.inc
        Automation::select_automation_view_parameter Automation::restore_previous_view Automation::buttonAction
    DEPENDS ${automation_selection_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE automation_selection_context_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/automation_selection_context.inc)

set(automation_navigation_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/automation_view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/automation_navigation_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${automation_navigation_source} ${CMAKE_CURRENT_BINARY_DIR}/automation_navigation_reset.inc
        AutomationView::reset_navigation_for_session_startup
    DEPENDS ${automation_navigation_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/automation_navigation_reset.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/automation_gesture_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${automation_navigation_source} ${CMAKE_CURRENT_BINARY_DIR}/automation_gesture_reset.inc
        AutomationView::reset_gestures_for_session_startup AutomationView::initPadSelection AutomationView::initInterpolation
        AutomationView::resetParameterShortcutBlinking AutomationView::resetInterpolationShortcutBlinking
        AutomationView::resetPadSelectionShortcutBlinking
    DEPENDS ${automation_navigation_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE automation_gesture_reset_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/automation_gesture_reset.inc)

set(note_selection_startup_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/instrument_clip_view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/note_selection_startup.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${note_selection_startup_source} ${CMAKE_CURRENT_BINARY_DIR}/note_selection_startup.inc
        InstrumentClipView::reset_selection_display_for_session_startup
        InstrumentClipView::resetSelectedNoteBlinking InstrumentClipView::resetSelectedNoteRowBlinking
    DEPENDS ${note_selection_startup_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE note_selection_startup_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/note_selection_startup.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/edit_pad_release.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${note_selection_startup_source} ${CMAKE_CURRENT_BINARY_DIR}/edit_pad_release.inc
        InstrumentClipView::endEditPadPress
    DEPENDS ${note_selection_startup_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE edit_pad_release_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/edit_pad_release.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/note_move_action_failure.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${note_selection_startup_source} ${CMAKE_CURRENT_BINARY_DIR}/note_move_action_failure.inc
        InstrumentClipView::scrollVertical_placeNotesPressed InstrumentClipView::scrollVertical_grabNotesPressed
    DEPENDS ${note_selection_startup_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE note_move_action_failure_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/note_move_action_failure.inc)

# Exercise screensaver routing with production event bodies and lightweight display/timer fixtures.
set(screensaver_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/display/screensaver.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/screensaver_events.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${screensaver_source} ${CMAKE_CURRENT_BINARY_DIR}/screensaver_events.inc
        Screensaver::arm Screensaver::noteActivity Screensaver::timerEvent Screensaver::settingsChanged
    DEPENDS ${screensaver_source} ../support/extract_functions.py VERBATIM)
add_executable(ScreensaverRoutingTests screensaver_routing_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/screensaver_events.inc)
target_include_directories(ScreensaverRoutingTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge)
target_link_libraries(ScreensaverRoutingTests PRIVATE CppUTest)
set_target_properties(ScreensaverRoutingTests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
add_test(NAME ScreensaverRoutingTests COMMAND ScreensaverRoutingTests)

set(sticky_setting_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/menu_item/runtime_feature/shift_is_sticky.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sticky_setting_write.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${sticky_setting_source} ${CMAKE_CURRENT_BINARY_DIR}/sticky_setting_write.inc ShiftIsSticky::writeCurrentValue
    DEPENDS ${sticky_setting_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/sticky_setting_write.inc ../../src/deluge/hid/button.cpp)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shift_led_feedback.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${button_reset_source} ${CMAKE_CURRENT_BINARY_DIR}/shift_led_feedback.inc update_shift_led
    DEPENDS ${button_reset_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shift_led_feedback.inc)

set(community_reset_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/settings/runtime_feature_settings.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/community_settings_reset.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${community_reset_source} ${CMAKE_CURRENT_BINARY_DIR}/community_settings_reset.inc RuntimeFeatureSettings::factoryReset
    DEPENDS ${community_reset_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/community_settings_reset.inc)

set(new_clip_type_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/context_menu/clip_settings/new_clip_type.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/new_clip_type_input.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${new_clip_type_source} ${CMAKE_CURRENT_BINARY_DIR}/new_clip_type_input.inc
        NewClipType::acceptCurrentOption NewClipType::padAction NewClipType::buttonAction
    DEPENDS ${new_clip_type_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE new_clip_type_input_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/new_clip_type_input.inc)

set(rename_clip_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/rename/rename_clip_ui.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rename_clip_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${rename_clip_source} ${CMAKE_CURRENT_BINARY_DIR}/rename_clip_methods.inc
        RenameClipUI::canRename RenameClipUI::getCurrentName RenameClipUI::trySetName
    DEPENDS ${rename_clip_source} ../support/extract_functions.py VERBATIM)
set(rename_song_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/song/song.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rename_clip_membership.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${rename_song_source} ${CMAKE_CURRENT_BINARY_DIR}/rename_clip_membership.inc Song::contains_clip_for_undo
    DEPENDS ${rename_song_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE rename_clip_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/rename_clip_methods.inc
    ${CMAKE_CURRENT_BINARY_DIR}/rename_clip_membership.inc)

set(rename_output_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/rename/rename_output_ui.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rename_output_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${rename_output_source} ${CMAKE_CURRENT_BINARY_DIR}/rename_output_methods.inc
        RenameOutputUI::canRename RenameOutputUI::getCurrentName RenameOutputUI::trySetName
    DEPENDS ${rename_output_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/rename_output_methods.inc)

set(rename_dialog_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/rename/rename_ui.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rename_dialog_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${rename_dialog_source} ${CMAKE_CURRENT_BINARY_DIR}/rename_dialog_methods.inc
        RenameUI::opened RenameUI::enterKeyPress
    DEPENDS ${rename_dialog_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE rename_dialog_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/rename_dialog_methods.inc)

set(ui_open_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/ui.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui_open.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${ui_open_source} ${CMAKE_CURRENT_BINARY_DIR}/ui_open.inc "bool openUI" "void closeUI" "bool changeUIAtLevel" "bool changeUISideways" "void changeRootUI" "void setRootUILowLevel" "void swapOutRootUILowLevel" "void uiNeedsRendering" "void clearPendingUIRendering" "void doAnyPendingGridRendering" "void doAnyPendingOLEDRendering" "void doAnyPendingUIRendering" getUIGreyoutColsAndRows
    DEPENDS ${ui_open_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ui_open_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/ui_open.inc)

set(rename_drum_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/rename/rename_drum_ui.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rename_drum_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${rename_drum_source} ${CMAKE_CURRENT_BINARY_DIR}/rename_drum_methods.inc
        RenameDrumUI::selected_drum_for_rename RenameDrumUI::canRename RenameDrumUI::getCurrentName RenameDrumUI::trySetName
    DEPENDS ${rename_drum_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/rename_drum_methods.inc)

set(rename_midi_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui/rename/rename_midi_cc_ui.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rename_midi_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${rename_midi_source} ${CMAKE_CURRENT_BINARY_DIR}/rename_midi_methods.inc
        RenameMidiCCUI::instrument_for_rename RenameMidiCCUI::canRename RenameMidiCCUI::getCurrentName RenameMidiCCUI::trySetName
    DEPENDS ${rename_midi_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/rename_midi_methods.inc)

set(midi_cc_name_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/instrument/midi_instrument.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/midi_cc_names.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${midi_cc_name_source} ${CMAKE_CURRENT_BINARY_DIR}/midi_cc_names.inc
        MIDIInstrument::setNameForCC MIDIInstrument::getNameFromCC
    DEPENDS ${midi_cc_name_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE midi_cc_name_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/midi_cc_names.inc)

set(greyout_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/hid/led/pad_leds.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/greyout_reassess.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${greyout_source} ${CMAKE_CURRENT_BINARY_DIR}/greyout_reassess.inc "void reassessGreyout"
    DEPENDS ${greyout_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/greyout_reassess.inc)

set(output_clip_led_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/output_clip_leds.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${output_clip_led_source} ${CMAKE_CURRENT_BINARY_DIR}/output_clip_leds.inc "static void set_output_clip_led_states"
    DEPENDS ${output_clip_led_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE output_clip_led_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/output_clip_leds.inc)

set(knob_indicator_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/view_knob_indicator.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/knob_indicator.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${knob_indicator_source} ${CMAKE_CURRENT_BINARY_DIR}/knob_indicator.inc
        View::setKnobIndicatorLevel View::convertPatchCableKnobPosToIndicatorLevel
    DEPENDS ${knob_indicator_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE knob_indicator_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/knob_indicator.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/knob_indicator_batch.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${view_startup_source} ${CMAKE_CURRENT_BINARY_DIR}/knob_indicator_batch.inc View::setKnobIndicatorLevels
    DEPENDS ${view_startup_source} ../support/extract_functions.py VERBATIM)
target_sources(UnitTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/knob_indicator_batch.inc)
