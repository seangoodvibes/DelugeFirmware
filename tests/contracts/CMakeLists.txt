find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME SessionRoutingContracts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/session_contracts.py)
add_test(NAME FunctionExtractionTests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extraction_tests.py)
