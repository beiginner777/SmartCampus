add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE common gtest_main)
add_test(NAME config COMMAND test_config)
