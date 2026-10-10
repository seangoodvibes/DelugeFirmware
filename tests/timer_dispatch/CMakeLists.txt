find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(timer_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/ui_timer_manager.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/timer_dispatch.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${timer_source} ${CMAKE_CURRENT_BINARY_DIR}/timer_dispatch.inc
        UITimerManager::routine UITimerManager::setTimer UITimerManager::setTimerSamples
        UITimerManager::unsetTimer UITimerManager::isTimerSet UITimerManager::workOutNextEventTime
    DEPENDS ${timer_source} ../support/extract_functions.py VERBATIM)
add_executable(TimerDispatchTests timer_dispatch_tests.cpp ${CMAKE_CURRENT_BINARY_DIR}/timer_dispatch.inc)
set_target_properties(TimerDispatchTests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(TimerDispatchTests PRIVATE mocks ${CMAKE_CURRENT_BINARY_DIR} ../../src/deluge)
target_compile_definitions(TimerDispatchTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_link_libraries(TimerDispatchTests PRIVATE CppUTest)
add_test(NAME TimerDispatchTests COMMAND TimerDispatchTests)

set(view_source ${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/gui/views/view.cpp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/view_automation.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../support/extract_functions.py
        ${view_source} ${CMAKE_CURRENT_BINARY_DIR}/view_automation.inc View::displayAutomation
    DEPENDS ${view_source} ../support/extract_functions.py VERBATIM)
target_sources(TimerDispatchTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/view_automation.inc)
