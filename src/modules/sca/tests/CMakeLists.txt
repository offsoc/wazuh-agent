find_package(GTest CONFIG REQUIRED)

add_executable(sca_test sca_test.cpp)
configure_target(sca_test)
target_link_libraries(sca_test PRIVATE SCA GTest::gtest GTest::gmock GTest::gtest_main)
add_test(NAME SCATest COMMAND sca_test)

add_executable(sca_policy_loader_test sca_policy_loader_test.cpp)
configure_target(sca_policy_loader_test)
target_include_directories(
    sca_policy_loader_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
                                   ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/file_helper/filesystem/tests/mocks)
target_link_libraries(sca_policy_loader_test PRIVATE SCA GTest::gtest GTest::gmock GTest::gtest_main)
add_test(NAME SCAPolicyLoaderTest COMMAND sca_policy_loader_test)

add_executable(sca_event_handler_test sca_event_handler_test.cpp)
configure_target(sca_event_handler_test)
target_include_directories(sca_event_handler_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(sca_event_handler_test PRIVATE SCA GTest::gtest GTest::gmock GTest::gtest_main)
add_test(NAME SCAEventHandlerTest COMMAND sca_event_handler_test)

add_executable(sca_utils_test sca_utils_test.cpp)
configure_target(sca_utils_test)
target_include_directories(sca_utils_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(sca_utils_test PRIVATE SCA GTest::gtest GTest::gtest_main)
add_test(NAME SCAUtilsTest COMMAND sca_utils_test)

add_executable(check_condition_evaluator_test check_condition_evaluator_test.cpp)
configure_target(check_condition_evaluator_test)
target_include_directories(check_condition_evaluator_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(check_condition_evaluator_test PRIVATE SCA GTest::gtest GTest::gtest_main)
add_test(NAME CheckConditionEvaluatorTest COMMAND check_condition_evaluator_test)

add_executable(rule_creation_test rule_creation_test.cpp)
configure_target(rule_creation_test)
target_include_directories(rule_creation_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(rule_creation_test PRIVATE SCA GTest::gtest GTest::gtest_main)
add_test(NAME RuleCreationTest COMMAND rule_creation_test)

add_executable(file_rule_evaluator_test file_rule_evaluator_test.cpp)
configure_target(file_rule_evaluator_test)
target_include_directories(
    file_rule_evaluator_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/file_helper/filesystem/tests/mocks
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/file_helper/file_io/tests/mocks)
target_link_libraries(file_rule_evaluator_test PRIVATE SCA GTest::gtest GTest::gmock GTest::gtest_main)
add_test(NAME FileRuleEvaluatorTest COMMAND file_rule_evaluator_test)

add_executable(command_rule_evaluator_test command_rule_evaluator_test.cpp)
configure_target(command_rule_evaluator_test)
target_include_directories(
    command_rule_evaluator_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
                                        ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/file_helper/filesystem/tests/mocks)
target_link_libraries(command_rule_evaluator_test PRIVATE SCA GTest::gtest GTest::gmock GTest::gtest_main)
add_test(NAME CommandRuleEvaluatorTest COMMAND command_rule_evaluator_test)

add_executable(directory_rule_evaluator_test directory_rule_evaluator_test.cpp)
configure_target(directory_rule_evaluator_test)
target_include_directories(
    directory_rule_evaluator_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/file_helper/filesystem/tests/mocks
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/file_helper/file_io/tests/mocks)
target_link_libraries(directory_rule_evaluator_test PRIVATE SCA GTest::gtest GTest::gmock GTest::gtest_main)
add_test(NAME DirectoryRuleEvaluatorTest COMMAND directory_rule_evaluator_test)

add_executable(process_rule_evaluator_test process_rule_evaluator_test.cpp)
configure_target(process_rule_evaluator_test)
target_include_directories(
    process_rule_evaluator_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/file_helper/filesystem/tests/mocks
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/data_provider/tests/mocks)
target_link_libraries(process_rule_evaluator_test PRIVATE SCA GTest::gtest GTest::gmock GTest::gtest_main)
add_test(NAME ProcessRuleEvaluatorTest COMMAND process_rule_evaluator_test)

add_executable(sca_policy_parser_test sca_policy_parser_test.cpp)
configure_target(sca_policy_parser_test)
target_include_directories(sca_policy_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(sca_policy_parser_test PRIVATE SCA GTest::gtest GTest::gtest_main)
add_test(NAME SCAPolicyParserTest COMMAND sca_policy_parser_test)

if(WIN32)
    add_executable(registry_rule_evaluator_test registry_rule_evaluator_test.cpp)
    configure_target(registry_rule_evaluator_test)
    target_include_directories(registry_rule_evaluator_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(registry_rule_evaluator_test PRIVATE SCA GTest::gtest GTest::gtest_main)
    add_test(NAME RegistryRuleEvaluatorTest COMMAND registry_rule_evaluator_test)
endif()
