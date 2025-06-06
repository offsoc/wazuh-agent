find_package(GTest CONFIG REQUIRED)

add_executable(http_client_test http_client_test.cpp)
configure_target(http_client_test)
target_include_directories(http_client_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(http_client_test PUBLIC HttpClient GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
add_test(NAME HttpClientTest COMMAND http_client_test)

add_executable(http_socket_test http_socket_test.cpp)
configure_target(http_socket_test)
target_include_directories(http_socket_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(http_socket_test PUBLIC Communicator GTest::gtest GTest::gtest_main GTest::gmock
                                              GTest::gmock_main Logger)
add_test(NAME HttpSocketTest COMMAND http_socket_test)

add_executable(https_socket_test https_socket_test.cpp)
configure_target(https_socket_test)
target_include_directories(https_socket_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
                                                     ${CMAKE_CURRENT_SOURCE_DIR}/../src/certificate)
target_link_libraries(https_socket_test PUBLIC Communicator GTest::gtest GTest::gtest_main GTest::gmock
                                               GTest::gmock_main Logger)
add_test(NAME HttpsSocketTest COMMAND https_socket_test)

if(WIN32)
    add_executable(https_verifier_test certificate/https_verifier_win_test.cpp)
    configure_target(https_verifier_test)
    target_include_directories(https_verifier_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/certificate)
    target_link_libraries(https_verifier_test PUBLIC HttpClient GTest::gtest GTest::gtest_main GTest::gmock
                                                     GTest::gmock_main)
    add_test(NAME HttpsVerifierTest COMMAND https_verifier_test)
elseif(APPLE)
    add_executable(https_verifier_test certificate/https_verifier_mac_test.cpp)
    configure_target(https_verifier_test)
    target_include_directories(https_verifier_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/certificate)
    target_link_libraries(https_verifier_test PUBLIC HttpClient GTest::gtest GTest::gtest_main GTest::gmock
                                                     GTest::gmock_main)
    add_test(NAME HttpsVerifierTest COMMAND https_verifier_test)
else()

endif()
