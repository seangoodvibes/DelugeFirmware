add_executable(RuntimeMenuTests runtime_menu_tests.cpp battery_menu_tests.cpp setting_cache_tests.cpp
    ../../src/deluge/gui/menu_item/runtime_feature/setting.cpp
    ../../src/deluge/gui/menu_item/runtime_feature/devSysexSetting.cpp)
set_target_properties(RuntimeMenuTests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(RuntimeMenuTests PRIVATE mocks ../../src/deluge)
target_compile_definitions(RuntimeMenuTests PRIVATE CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_link_libraries(RuntimeMenuTests PRIVATE CppUTest)
add_test(NAME RuntimeMenuTests COMMAND RuntimeMenuTests)
