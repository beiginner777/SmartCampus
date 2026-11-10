add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE common gtest_main)
add_test(NAME config COMMAND test_config)

add_executable(test_codec test_codec.cpp)
target_link_libraries(test_codec PRIVATE common gtest_main)
add_test(NAME codec COMMAND test_codec)

add_executable(test_jwt test_jwt.cpp)
target_link_libraries(test_jwt PRIVATE common gtest_main)
add_test(NAME jwt COMMAND test_jwt)
