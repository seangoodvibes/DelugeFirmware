# Compile production parameter ownership, automation and undo; platform.cpp supplies
# host allocation, UI/action hooks and a deterministic timeline.
add_executable(NativeParameterLifecycleTests
    ../../../unit/RunAllTests.cpp lifecycle_tests.cpp platform.cpp
    ../native/platform.cpp
    ../../../../src/deluge/modulation/patch/patch_cable.cpp
    ../../../../src/deluge/modulation/patch/patch_cable_set.cpp
    ../../../../src/deluge/util/algorithm/quick_sorter.cpp
    ../../../../src/deluge/util/patch_source.cpp
    ../../../../src/deluge/gui/views/automation/editor_layout/mod_controllable/parameter_edit.cpp
    ../../../../src/deluge/gui/views/view_knob_indicator.cpp
    ../../../../src/deluge/modulation/params/param.cpp
    ../../../../src/deluge/model/timeline_counter.cpp
    ../../../../src/deluge/storage/Deserializer.cpp
    ../../../../src/deluge/storage/JsonDeserializer.cpp
    ../../../../src/deluge/storage/Serializer.cpp
    ../../../../src/deluge/storage/JSONSerializer.cpp
    ../../../../src/deluge/storage/file_reader.cpp
    ../../../../src/deluge/util/d_stringbuf.cpp
    ../../../../src/deluge/util/cfunctions.c
    ../../../../src/deluge/util/semver.cpp
    ../../../../src/deluge/modulation/automation/auto_param.cpp
    ../../../../src/deluge/modulation/automation/auto_param_pool.cpp
    ../../../../src/deluge/modulation/params/param_set.cpp
    ../../../../src/deluge/modulation/params/param_set_param_lookup.cpp
    ../../../../src/deluge/modulation/params/param_collection.cpp
    ../../../../src/deluge/modulation/params/param_manager.cpp
    ../../../../src/deluge/modulation/params/param_manager_setup.cpp
    ../../../../src/deluge/modulation/params/param_manager_transfer.cpp
    ../../../../src/deluge/modulation/params/param_manager_cleanup.cpp
    ../../../../src/deluge/modulation/params/param_manager_type.cpp
    ../../../../src/deluge/modulation/params/param_node_vector.cpp
    ../../../../src/deluge/model/model_stack_param_lookup.cpp
    ../../../../src/deluge/util/container/list/bidirectional_linked_list.cpp
    ../../../../src/deluge/model/consequence/consequence.cpp
    ../../../../src/deluge/model/consequence/consequence_param_change.cpp
    ../../../../src/deluge/util/container/array/resizeable_array.cpp
    ../../../../src/deluge/util/container/array/ordered_resizeable_array_with_multi_word_key.cpp
    ../../../../src/deluge/util/container/array/ordered_resizeable_array.cpp
)
target_compile_features(NativeParameterLifecycleTests PRIVATE cxx_std_23)
set_target_properties(NativeParameterLifecycleTests PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON)
target_compile_definitions(NativeParameterLifecycleTests PRIVATE IN_UNIT_TESTS=1 CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_include_directories(NativeParameterLifecycleTests PRIVATE mocks
    ../../../32bit_unit_tests/mocks ../../../../src/deluge ../../../../src ../../../../src/NE10/inc)
target_link_libraries(NativeParameterLifecycleTests PRIVATE CppUTest etl::etl)
# Unused firmware entry points reference unrelated audio/UI services. Discard
# those functions while retaining the actual ParamSet virtual methods.
target_compile_options(NativeParameterLifecycleTests PRIVATE -ffunction-sections -fdata-sections
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-invalid-constexpr>)
if(APPLE)
    target_link_options(NativeParameterLifecycleTests PRIVATE -Wl,-dead_strip)
else()
    target_link_options(NativeParameterLifecycleTests PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME NativeParameterLifecycleTests COMMAND NativeParameterLifecycleTests)

option(PARAMETER_LIFECYCLE_ASAN "Check native parameter lifetimes with AddressSanitizer" OFF)
if(PARAMETER_LIFECYCLE_ASAN)
    target_compile_options(NativeParameterLifecycleTests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(NativeParameterLifecycleTests PRIVATE -fsanitize=address)
endif()
