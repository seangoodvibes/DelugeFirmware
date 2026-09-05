add_executable(NativeParameterPersistenceTests
    ../../../unit/RunAllTests.cpp
    persistence_tests.cpp
    platform.cpp
    ../../../../src/deluge/storage/Deserializer.cpp
    ../../../../src/deluge/storage/JsonDeserializer.cpp
    ../../../../src/deluge/storage/Serializer.cpp
    ../../../../src/deluge/storage/JSONSerializer.cpp
    ../../../../src/deluge/storage/file_reader.cpp
    ../../../../src/deluge/util/d_stringbuf.cpp
    ../../../../src/deluge/util/cfunctions.c
    ../../../../src/deluge/util/semver.cpp
)
target_compile_features(NativeParameterPersistenceTests PRIVATE cxx_std_23)
set_target_properties(NativeParameterPersistenceTests PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON)
target_compile_definitions(NativeParameterPersistenceTests PRIVATE
    IN_UNIT_TESTS=1 CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_include_directories(NativeParameterPersistenceTests PRIVATE
    ../../../32bit_unit_tests/mocks
    ../../../../src/deluge
    ../../../../src)
target_link_libraries(NativeParameterPersistenceTests PRIVATE CppUTest etl::etl)
add_test(NAME NativeParameterPersistenceTests COMMAND NativeParameterPersistenceTests)