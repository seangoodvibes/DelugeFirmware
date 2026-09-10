find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(clip_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/clip/instrument_clip.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clip_repeat_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${clip_source} ${CMAKE_CURRENT_BINARY_DIR}/clip_repeat_methods.inc
        InstrumentClip::increaseLengthWithRepeats InstrumentClip::repeatOrChopToExactLength InstrumentClip::halveNoteRowsWithIndependentLength
    DEPENDS ${clip_source} ../support/extract_functions.py VERBATIM)
add_executable(ClipRepeatTests ../unit/RunAllTests.cpp clip_repeat_tests.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/clip_repeat_methods.inc)
target_include_directories(ClipRepeatTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge)
target_link_libraries(ClipRepeatTests PRIVATE CppUTestExt)
target_compile_features(ClipRepeatTests PRIVATE cxx_std_23)
target_compile_definitions(ClipRepeatTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
if(UNDO_TEST_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ClipRepeatTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ClipRepeatTests PRIVATE -fsanitize=address,undefined)
endif()
add_dependencies(UnitTests ClipRepeatTests)
add_test(NAME ClipRepeatTests COMMAND ClipRepeatTests)

set(song_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/song/song.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/song_double_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${song_source} ${CMAKE_CURRENT_BINARY_DIR}/song_double_method.inc Song::doubleClipLength
    DEPENDS ${song_source} ../support/extract_functions.py VERBATIM)
target_sources(ClipRepeatTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/song_double_method.inc)

set(multiply_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/consequence/consequence_instrument_clip_multiply.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/multiply_revert_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${multiply_source} ${CMAKE_CURRENT_BINARY_DIR}/multiply_revert_method.inc ConsequenceInstrumentClipMultiply::revert
    DEPENDS ${multiply_source} ../support/extract_functions.py VERBATIM)
target_sources(ClipRepeatTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/multiply_revert_method.inc)

set(row_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/note/note_row.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/row_length_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${row_source} ${CMAKE_CURRENT_BINARY_DIR}/row_length_method.inc NoteRow::setLength
    DEPENDS ${row_source} ../support/extract_functions.py VERBATIM)
target_sources(ClipRepeatTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/row_length_method.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/song_length_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${song_source} ${CMAKE_CURRENT_BINARY_DIR}/song_length_method.inc Song::setClipLength
    DEPENDS ${song_source} ../support/extract_functions.py VERBATIM)
target_sources(ClipRepeatTests PRIVATE song_length_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/song_length_method.inc)

set(audio_view_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/audio_clip_view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/audio_start_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${audio_view_source} ${CMAKE_CURRENT_BINARY_DIR}/audio_start_method.inc
        AudioClipView::changeUnderlyingSampleStart AudioClipView::changeUnderlyingSampleLength
    DEPENDS ${audio_view_source} ../support/extract_functions.py VERBATIM)
target_sources(ClipRepeatTests PRIVATE audio_start_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/audio_start_method.inc)

set(clip_view_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/clip_view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clip_view_length_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${clip_view_source} ${CMAKE_CURRENT_BINARY_DIR}/clip_view_length_methods.inc
        ClipView::lengthenClip ClipView::shortenClip ClipView::changeClipLength
    DEPENDS ${clip_view_source} ../support/extract_functions.py VERBATIM)
target_sources(ClipRepeatTests PRIVATE clip_view_length_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/clip_view_length_methods.inc)

set(playback_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/playback/playback_handler.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tempoless_finish_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${playback_source} ${CMAKE_CURRENT_BINARY_DIR}/tempoless_finish_method.inc
        PlaybackHandler::finishTempolessRecording
    DEPENDS ${playback_source} ../support/extract_functions.py VERBATIM)
target_sources(ClipRepeatTests PRIVATE tempoless_finish_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/tempoless_finish_method.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tempoless_tempo_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${playback_source} ${CMAKE_CURRENT_BINARY_DIR}/tempoless_tempo_method.inc
        PlaybackHandler::setTempoFromAudioClipLength
    DEPENDS ${playback_source} ../support/extract_functions.py VERBATIM)
target_sources(ClipRepeatTests PRIVATE tempoless_tempo_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/tempoless_tempo_method.inc)
