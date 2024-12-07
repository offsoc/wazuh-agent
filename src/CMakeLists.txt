cmake_minimum_required(VERSION 3.22)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
get_filename_component(SRC_FOLDER ${CMAKE_SOURCE_DIR}/../ ABSOLUTE)
get_filename_component(CONFIG_FOLDER ${CMAKE_SOURCE_DIR}/../etc/config/ ABSOLUTE)
project(wazuh-agent)

include(cmake/CommonSettings.cmake)
set_common_settings()

if(ENABLE_LOGCOLLECTOR OR ENABLE_INVENTORY)
    add_subdirectory(common)
else()
    add_subdirectory(common/logger)
    add_subdirectory(common/utils)
    add_subdirectory(common/data_provider)
endif()
add_subdirectory(modules)
add_subdirectory(agent)

add_executable(wazuh-agent agent/src/main.cpp)
target_link_libraries(wazuh-agent Agent Logger)

if(WIN32)
    include(cmake/SetWindowsManifest.cmake)
    set_windows_manifest()
endif()

include(cmake/ConfigureTarget.cmake)
configure_target(wazuh-agent)

## Installation
set(INSTALL_ROOT "" CACHE PATH "Root directory for installation")

if("${INSTALL_ROOT}" STREQUAL "/")
    set(INSTALL_ROOT "")
endif()

install(CODE "file(MAKE_DIRECTORY ${INSTALL_ROOT}${BIN_INSTALL_DIR})")

install(TARGETS wazuh-agent
        DESTINATION ${INSTALL_ROOT}${BIN_INSTALL_DIR})

install(CODE "file(MAKE_DIRECTORY ${INSTALL_ROOT}${DATA_INSTALL_DIR})")
install(CODE "file(MAKE_DIRECTORY ${INSTALL_ROOT}${CONFIG_INSTALL_DIR})")
install(CODE "file(MAKE_DIRECTORY ${INSTALL_ROOT}${LIB_INSTALL_DIR})")
install(FILES ${CONFIG_FOLDER}/wazuh-agent.yml
        DESTINATION ${INSTALL_ROOT}${CONFIG_INSTALL_DIR})
install(TARGETS wazuh-agent
         DESTINATION ${INSTALL_ROOT}${BIN_INSTALL_DIR})

message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SOURCE_FILE "${CMAKE_SOURCE_DIR}/agent/service/wazuh-agent.service")
    set(OUTPUT_FILE "${CMAKE_BINARY_DIR}/wazuh-agent.service")

    file(READ "${SOURCE_FILE}" FILE_CONTENTS)
    string(REPLACE "WAZUH_HOME" "${BIN_INSTALL_DIR}" FILE_CONTENTS "${FILE_CONTENTS}")
    file(WRITE "${OUTPUT_FILE}" "${FILE_CONTENTS}")

    add_custom_target(generate_service_file ALL DEPENDS "${OUTPUT_FILE}")
    install(FILES "${OUTPUT_FILE}"
        DESTINATION "${INSTALL_ROOT}${SERVICE_INSTALL_DIR}")
endif()

if(BUILD_TESTS)
    enable_testing()
endif()
