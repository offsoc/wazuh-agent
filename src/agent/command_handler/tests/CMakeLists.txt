find_package(GTest CONFIG REQUIRED)

add_executable(command_handler_test command_handler_test.cpp)
configure_target(command_handler_test)
target_link_libraries(command_handler_test PUBLIC CommandHandler GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
add_test(NAME CommandHandlerTest COMMAND command_handler_test)
