/*
 * Wazuh Syscheck - Test tool
 * Copyright (C) 2015, Wazuh Inc.
 * January 21, 2022.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include <fstream>
#include <ostream>
#include <memory>
#include <json.hpp>
#include "db.hpp"
#include "fimDB.hpp"
#include "cmdArgsHelper.h"
#include "testContext.h"
#include "action.h"
#include "factoryAction.h"

static void syncCallback(const char* tag, const char* msg)
{
    std::cout << tag << ": " << msg;
}

static void loggerFunction(modules_log_level_t level, const char* msg)
{
    std::cout << "Level:" << level << " Msg: " << msg << '\n';
}

int main(int argc, const char* argv[])
{
    try
    {
        CmdLineArgs cmdLineArgs(argc, argv);

        const auto actions { cmdLineArgs.actions() };

        std::cout << "Actions: " << actions.size() << '\n';

        std::ifstream configFile{ cmdLineArgs.configFile() };

        if (configFile.good())
        {
            const auto& jsonConfigFile { nlohmann::json::parse(configFile) };
            const auto storageType{ jsonConfigFile.at("storage_type").get<const uint32_t>() };
            const auto syncInterval{ jsonConfigFile.at("sync_interval").get<const uint32_t>() };
            const auto fileLimit{ jsonConfigFile.at("file_limit").get<const uint32_t>() };
            const auto registryLimit{ jsonConfigFile.at("registry_limit").get<const uint32_t>() };
            const bool syncRegistryEnabled { jsonConfigFile.at("registry_sync") };
            const auto syncResponseTimeout{ jsonConfigFile.at("sync_response_timeout").get<const uint32_t>() };
            const auto syncMaxInterval{ jsonConfigFile.at("sync_max_interval").get<const uint32_t>() };
            const auto syncThreadPool{ jsonConfigFile.at("thread_pool").get<const uint32_t>() };
            const auto syncQueueSize{ jsonConfigFile.at("queue_size").get<const uint32_t>() };

            std::function<void(const std::string&)> callbackSyncFileWrapper
            {
                [](const std::string & msg)
                {
                    syncCallback(FIM_COMPONENT_FILE, msg.c_str());
                }
            };

            std::function<void(const std::string&)> callbackSyncRegistryWrapper
            {
                [](const std::string & msg)
                {
                    syncCallback(FIM_COMPONENT_REGISTRY_KEY, msg.c_str());
                }
            };

            std::function<void(modules_log_level_t, const std::string&)> callbackLogWrapper
            {
                [](modules_log_level_t level, const std::string & log)
                {
                    loggerFunction(level, log.c_str());
                }
            };

            try
            {
                DB::instance().init(storageType,
                                    syncInterval,
                                    syncMaxInterval,
                                    syncResponseTimeout,
                                    callbackSyncFileWrapper,
                                    callbackSyncRegistryWrapper,
                                    callbackLogWrapper,
                                    fileLimit,
                                    registryLimit,
                                    syncRegistryEnabled,
                                    syncThreadPool,
                                    syncQueueSize);

                std::unique_ptr<TestContext> testContext { std::make_unique<TestContext>()};
                testContext->outputPath = cmdLineArgs.outputFolder();

                // Let's take the input json list and apply the changes to the db
                for (size_t idx = 0; idx < actions.size(); ++idx)
                {
                    testContext->currentId = idx;
                    const std::string inputFile{ actions[idx] };
                    std::ifstream actionsIdxFile{ inputFile };
                    std::cout << "Processing file: " << inputFile << '\n';
                    const auto& jsonAction { nlohmann::json::parse(actionsIdxFile) };
                    auto action { FactoryAction::create(jsonAction.at("action").get<std::string>()) };
                    action->execute(testContext, jsonAction.at("body"));
                }

                DB::instance().teardown();
                std::cout << "Resulting files are located in the "
                          << cmdLineArgs.outputFolder() << " folder" << '\n';
            }
            catch (const std::exception& e)
            {
                std::cerr << '\n'
                          << "Something went wrong configuring the database. Please, check the config file data, "
                          << e.what() << '\n';
            }
        }
        else
        {
            throw std::runtime_error { "The config file is not valid. Please, check the config file data" };
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << '\n';
        CmdLineArgs::showHelp();
    }

    return 0;
}
