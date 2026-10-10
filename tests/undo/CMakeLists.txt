# Compile real undo consequences against bounded model doubles. Keeping this in
# its own executable prevents the doubles from leaking into the normal suite.
set(undo_sources
    consequence consequence_clip_horizontal_shift consequence_clip_length
    consequence_note_array_change consequence_note_row_horizontal_shift
    consequence_note_row_length consequence_note_row_mute consequence_note_existence)
add_executable(UndoConsequenceTests ../unit/RunAllTests.cpp consequence_tests.cpp model_tests.cpp note_existence_tests.cpp)
foreach(source IN LISTS undo_sources)
    target_sources(UndoConsequenceTests PRIVATE ../../src/deluge/model/consequence/${source}.cpp)
endforeach()
target_include_directories(UndoConsequenceTests PRIVATE mocks ../../src ../../src/deluge)
target_link_libraries(UndoConsequenceTests PRIVATE CppUTestExt etl::etl)
target_compile_features(UndoConsequenceTests PRIVATE cxx_std_23)
target_compile_definitions(UndoConsequenceTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED IN_UNIT_TESTS=1)
add_test(NAME UndoConsequenceTests COMMAND UndoConsequenceTests)
# The established native build command must also compile this regression suite.
add_dependencies(UnitTests UndoConsequenceTests)
option(UNDO_TEST_SANITIZERS "Run undo collaborators with address/undefined sanitizers" OFF)
if(UNDO_TEST_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(UndoConsequenceTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(UndoConsequenceTests PRIVATE -fsanitize=address,undefined)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(song_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/song/song.cpp)
set(audio_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/clip/audio_clip.cpp)
foreach(kind song audio)
    if(kind STREQUAL "song")
        set(methods Song::contains_clip_for_undo Song::get_clip_index_for_undo Song::owns_output_for_undo Song::can_reference_clip_from_output)
    else()
        set(methods AudioClip::can_shift_horizontally)
    endif()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${kind}_methods.inc
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
            ${${kind}_source} ${CMAKE_CURRENT_BINARY_DIR}/${kind}_methods.inc ${methods}
        DEPENDS ${${kind}_source} ../support/extract_functions.py VERBATIM)
    target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${kind}_methods.inc)
endforeach()
target_include_directories(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set(action_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/action/action.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/action_snapshot_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${action_source} ${CMAKE_CURRENT_BINARY_DIR}/action_snapshot_method.inc
        Action::containsConsequenceParamChange Action::recordParamChangeIfNotAlreadySnapshotted Action::record_param_change Action::recordParamChangeDefinitely Action::recordNoteArrayChangeIfNotAlreadySnapshotted Action::recordNoteExistenceChange Action::allocate_note_existence_memory Action::record_note_array_change Action::recordNoteArrayChangeDefinitely Action::containsConsequenceNoteArrayChange Action::recordClipExistenceChange Action::rollback_empty_instance_deletions Action::rollback_instance_batch Action::record_arranger_time_inserted Action::rollback_arranger_time_inserted Action::recordClipLengthChange Action::recordClipInstanceExistenceChange Action::revert
    DEPENDS ${action_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE action_snapshot_tests.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/action_snapshot_method.inc)

set(note_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/note/note.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/note_constructor.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${note_source} ${CMAKE_CURRENT_BINARY_DIR}/note_constructor.inc Note::Note
    DEPENDS ${note_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/note_constructor.inc)

set(clip_existence_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/consequence/consequence_clip_existence.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clip_recreate_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${clip_existence_source} ${CMAKE_CURRENT_BINARY_DIR}/clip_recreate_method.inc
        ConsequenceClipExistence::prepareForDestruction ConsequenceClipExistence::can_recreate ConsequenceClipExistence::reserve_for_recreation ConsequenceClipExistence::commit_recreation ConsequenceClipExistence::reattach_for_recreation
    DEPENDS ${clip_existence_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/clip_recreate_method.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/exact_backup_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/song/song_param_manager_backups.cpp ${CMAKE_CURRENT_BINARY_DIR}/exact_backup_method.inc
        Song::getBackedUpParamManagerForExactClip
    DEPENDS ../../src/deluge/model/song/song_param_manager_backups.cpp ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/exact_backup_method.inc)

set(kit_restore_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/clip/instrument_clip.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/kit_restore_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${kit_restore_source} ${CMAKE_CURRENT_BINARY_DIR}/kit_restore_method.inc
        InstrumentClip::undoUnassignmentOfAllNoteRowsFromDrums InstrumentClip::undoDetachmentFromOutput
    DEPENDS ${kit_restore_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE kit_restore_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/kit_restore_method.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clip_restore_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/clip/clip.cpp ${CMAKE_CURRENT_BINARY_DIR}/clip_restore_method.inc
        Clip::undoDetachmentFromOutput
    DEPENDS ../../src/deluge/model/clip/clip.cpp ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/clip_restore_method.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/note_delete_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/note/note_row.cpp ${CMAKE_CURRENT_BINARY_DIR}/note_delete_method.inc
        NoteRow::deleteNoteByIndex NoteRow::trimNoteDataToNewClipLength NoteRow::changeNotesAcrossAllScreens
        NoteRow::clearArea NoteRow::addCorrespondingNotes NoteRow::editNoteRepeatAcrossAllScreens
        NoteRow::nudgeNotesAcrossAllScreens NoteRow::complexSetNoteLength NoteRow::clear NoteRow::recordNoteOff NoteRow::trimToLength NoteRow::stopCurrentlyPlayingNote NoteRow::generateRepeats
    DEPENDS ../../src/deluge/model/note/note_row.cpp ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/note_delete_method.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/iterance_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/iterance/iterance.cpp ${CMAKE_CURRENT_BINARY_DIR}/iterance_method.inc
        Iterance::fromInt
    DEPENDS ../../src/deluge/model/iterance/iterance.cpp ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/iterance_method.inc)

set(arranger_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/arranger_view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/arranger_delete_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${arranger_source} ${CMAKE_CURRENT_BINARY_DIR}/arranger_delete_method.inc ArrangerView::delete_clip_instance ArrangerView::shift_clips_horizontally ArrangerView::shift_arrangement_time
    DEPENDS ${arranger_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/arranger_delete_method.inc)

set(instance_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/clip/clip_instance.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/instance_change_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${instance_source} ${CMAKE_CURRENT_BINARY_DIR}/instance_change_method.inc ClipInstance::change
    DEPENDS ${instance_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/instance_change_method.inc)

set(instance_consequence_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/consequence/consequence_clip_instance_change.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/instance_change_consequence.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${instance_consequence_source} ${CMAKE_CURRENT_BINARY_DIR}/instance_change_consequence.inc
        ConsequenceClipInstanceChange::ConsequenceClipInstanceChange ConsequenceClipInstanceChange::revert
    DEPENDS ${instance_consequence_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/instance_change_consequence.inc)

set(instance_existence_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/model/consequence/consequence_clip_instance_existence.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/instance_existence_consequence.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${instance_existence_source} ${CMAKE_CURRENT_BINARY_DIR}/instance_existence_consequence.inc
        ConsequenceClipInstanceExistence::ConsequenceClipInstanceExistence ConsequenceClipInstanceExistence::revert
    DEPENDS ${instance_existence_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/instance_existence_consequence.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/song_cleanup_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${song_source} ${CMAKE_CURRENT_BINARY_DIR}/song_cleanup_method.inc Song::deletingClipInstanceForClip
    DEPENDS ${song_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE song_cleanup_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/song_cleanup_method.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clip_delete_preparation.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${clip_existence_source} ${CMAKE_CURRENT_BINARY_DIR}/clip_delete_preparation.inc
        ConsequenceClipExistence::prepare_for_deletion
    DEPENDS ${clip_existence_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE clip_deletion_tests.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/clip_delete_preparation.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clip_existence_revert.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${clip_existence_source} ${CMAKE_CURRENT_BINARY_DIR}/clip_existence_revert.inc
        ConsequenceClipExistence::revert
    DEPENDS ${clip_existence_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE clip_existence_revert_tests.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/clip_existence_revert.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/song_teardown_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${song_source} ${CMAKE_CURRENT_BINARY_DIR}/song_teardown_methods.inc
        Song::~Song Song::contains_clip_for_undo
    DEPENDS ${song_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE song_teardown_tests.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/song_teardown_methods.inc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/output_teardown_method.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${song_source} ${CMAKE_CURRENT_BINARY_DIR}/output_teardown_method.inc Song::deleteAllOutputs
    DEPENDS ${song_source} ../support/extract_functions.py VERBATIM)
target_sources(UndoConsequenceTests PRIVATE output_teardown_tests.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/output_teardown_method.inc)
