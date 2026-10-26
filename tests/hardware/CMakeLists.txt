find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME OledCVDriverTests
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oled_cv/run.py --cc ${CMAKE_C_COMPILER})
