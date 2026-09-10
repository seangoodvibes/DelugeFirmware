add_executable(MirrorRuntimeTests ../unit/RunAllTests.cpp runtime_tests.cpp indicator_tests.cpp usb_queue_tests.cpp)
target_include_directories(MirrorRuntimeTests PRIVATE mocks ../../src ../../src/deluge)
target_link_libraries(MirrorRuntimeTests PRIVATE CppUTestExt etl::etl)
target_compile_features(MirrorRuntimeTests PRIVATE cxx_std_23)
target_compile_definitions(MirrorRuntimeTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED IN_UNIT_TESTS=1)
add_test(NAME MirrorRuntimeTests COMMAND MirrorRuntimeTests)
add_dependencies(UnitTests MirrorRuntimeTests)
if(UNDO_TEST_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(MirrorRuntimeTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(MirrorRuntimeTests PRIVATE -fsanitize=address,undefined)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(queue_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/io/midi/midi_device_manager.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/usb_queue_methods.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${queue_source} ${CMAKE_CURRENT_BINARY_DIR}/usb_queue_methods.inc
        ConnectedUSBMIDIDevice::setup ConnectedUSBMIDIDevice::bufferMessage ConnectedUSBMIDIDevice::hasBufferedSendData
        ConnectedUSBMIDIDevice::sendBufferSpace ConnectedUSBMIDIDevice::consumeSendData ConnectedUSBMIDIDevice::discard_queued_non_sys_ex
    DEPENDS ${queue_source} ../support/extract_functions.py VERBATIM)
target_sources(MirrorRuntimeTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/usb_queue_methods.inc)
target_include_directories(MirrorRuntimeTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
