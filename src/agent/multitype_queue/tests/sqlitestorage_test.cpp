#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

#include "sqlitestorage.hpp"

class SQLiteStorageTest : public ::testing::Test
{
protected:
    const std::string dbName = "testdb.db";
    const std::string tableName = "test_table";
    const std::string moduleName = "moduleX";
    const std::vector<std::string> m_vMessageTypeStrings {"test_table", "test_table2"};
    std::unique_ptr<SQLiteStorage> storage;

    void SetUp() override
    {
        // Ensure the database file does not already exist
        for (const auto& entry : std::filesystem::directory_iterator("."))
        {
            const auto fileFullPath = entry.path().string();
            if (fileFullPath.find(dbName) != std::string::npos)
            {
                std::error_code ec;
                std::filesystem::remove(fileFullPath, ec);
            }
        }

        storage = std::make_unique<SQLiteStorage>(dbName, m_vMessageTypeStrings);
    }

    void TearDown() override
    {
        // Clean up: remove the database file
        std::error_code ec;
        if (std::filesystem::exists(dbName.c_str()))
        {
            std::filesystem::remove(dbName.c_str(), ec);
            if (ec)
            {
                std::cerr << "Error removing file: " << ec.message() << '\n';
            }
        }
    }
};

TEST_F(SQLiteStorageTest, StoreSingleMessage)
{
    const nlohmann::json message = {{"key", "value"}};
    EXPECT_EQ(storage->Store(message, tableName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 1);
    EXPECT_EQ(storage->Store(message, tableName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 2);
}

TEST_F(SQLiteStorageTest, StoreSingleMessageWithModule)
{
    const nlohmann::json message = {{"key", "value"}};
    EXPECT_EQ(storage->Store(message, tableName, moduleName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 1);
    EXPECT_EQ(storage->Store(message, tableName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 2);
    EXPECT_EQ(storage->GetElementCount(tableName, "unavailableModuleName"), 0);
    EXPECT_EQ(storage->GetElementCount(tableName, moduleName), 1);
}

TEST_F(SQLiteStorageTest, StoreMultipleMessages)
{
    auto messages = nlohmann::json::array();
    messages.push_back({{"key", "value1"}});
    messages.push_back({{"key", "value2"}});
    EXPECT_EQ(storage->Store(messages, tableName), 2);
    EXPECT_EQ(storage->GetElementCount(tableName), 2);
}

TEST_F(SQLiteStorageTest, StoreMultipleMessagesWithModule)
{
    auto messages = nlohmann::json::array();
    messages.push_back({{"key", "value1"}});
    messages.push_back({{"key", "value2"}});
    EXPECT_EQ(storage->Store(messages, tableName, moduleName), 2);
    EXPECT_EQ(storage->GetElementCount(tableName), 2);
    EXPECT_EQ(storage->GetElementCount(tableName, moduleName), 2);
    EXPECT_EQ(storage->GetElementCount(tableName, "unavailableModuleName"), 0);
}

TEST_F(SQLiteStorageTest, RetrieveMessage)
{
    const nlohmann::json message = {{"key", "value"}};
    EXPECT_EQ(storage->Store(message, tableName), 1);
    const auto retrievedMessage = storage->Retrieve(1, tableName);
    EXPECT_EQ(retrievedMessage.at("data").at("key"), "value");
}

TEST_F(SQLiteStorageTest, RetrieveMessageWithModule)
{
    nlohmann::json message = {{"key", "value"}};
    storage->Store(message, tableName, moduleName);
    auto retrievedMessage = storage->Retrieve(1, tableName, "unavailableModuleName");
    EXPECT_EQ(retrievedMessage.at("data"), nullptr);
    retrievedMessage = storage->Retrieve(1, tableName, moduleName);
    EXPECT_EQ(retrievedMessage.at("data").at("key"), "value");
}

TEST_F(SQLiteStorageTest, RetrieveMultipleMessages)
{
    auto messages = nlohmann::json::array();
    messages.push_back({{"key", "value1"}});
    messages.push_back({{"key", "value2"}});
    storage->Store(messages, tableName);

    const auto retrievedMessages = storage->RetrieveMultiple(2, tableName);
    EXPECT_EQ(retrievedMessages.size(), 2);
}

TEST_F(SQLiteStorageTest, RetrieveMultipleMessagesWithModule)
{
    auto messages = nlohmann::json::array();
    messages.push_back({{"key", "value1"}});
    messages.push_back({{"key", "value2"}});
    messages.push_back({{"key", "value3"}});
    messages.push_back({{"key", "value4"}});
    storage->Store(messages, tableName, moduleName);

    const auto retrievedMessages = storage->RetrieveMultiple(4, tableName, moduleName);
    EXPECT_EQ(retrievedMessages.size(), 4);

    int i = 0;
    for (auto singleMessage : retrievedMessages)
    {
        EXPECT_EQ("value" + std::to_string(++i), singleMessage.at("data").at("key").get<std::string>());
    }
}

TEST_F(SQLiteStorageTest, RemoveMessage)
{
    nlohmann::json message = {{"key", "value"}};
    EXPECT_EQ(storage->Store(message, tableName), 1);
    EXPECT_EQ(storage->Remove(1, tableName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 0);
}

TEST_F(SQLiteStorageTest, RemoveMessageWithModule)
{
    nlohmann::json message = {{"key", "value"}};
    EXPECT_EQ(storage->Store(message, tableName, moduleName), 1);
    EXPECT_EQ(storage->Remove(1, tableName), 1);
    EXPECT_EQ(storage->Store(message, tableName, moduleName), 1);
    EXPECT_EQ(storage->Remove(1, tableName, "unavailableModuleName"), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 1);
    EXPECT_EQ(storage->Remove(1, tableName, moduleName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 0);
}

TEST_F(SQLiteStorageTest, RemoveMultipleMessages)
{
    auto messages = nlohmann::json::array();
    messages.push_back({{"key", "value1"}});
    messages.push_back({{"key", "value2"}});
    EXPECT_EQ(storage->Store(messages, tableName), 2);
    EXPECT_EQ(storage->RemoveMultiple(2, tableName), 2);
    EXPECT_EQ(storage->GetElementCount(tableName), 0);
}

TEST_F(SQLiteStorageTest, RemoveMultipleMessagesWithModule)
{
    auto messages = nlohmann::json::array();
    messages.push_back({{"key", "value1"}});
    messages.push_back({{"key", "value2"}});
    EXPECT_EQ(storage->Store(messages, tableName, moduleName), 2);
    EXPECT_EQ(storage->RemoveMultiple(2, tableName, "unavailableModuleName"), 0);
    EXPECT_EQ(storage->GetElementCount(tableName), 2);
    EXPECT_EQ(storage->RemoveMultiple(2, tableName, moduleName), 2);
    EXPECT_EQ(storage->GetElementCount(tableName), 0);
}

TEST_F(SQLiteStorageTest, GetElementCount)
{
    nlohmann::json message = {{"key", "value"}};
    EXPECT_EQ(storage->Store(message, tableName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 1);
}

TEST_F(SQLiteStorageTest, GetElementCountWithModule)
{
    nlohmann::json message = {{"key", "value"}};
    EXPECT_EQ(storage->Store(message, tableName, moduleName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName, moduleName), 1);
    EXPECT_EQ(storage->GetElementCount(tableName, "unavailableModuleName"), 0);
}

class SQLiteStorageMultithreadedTest : public ::testing::Test
{
protected:
    const std::string dbName = "testdb";
    const std::vector<std::string> m_vMessageTypeStrings {"test_table1", "test_table2", "test_table3"};

    void SetUp() override
    {
        // Clean up: Delete the SQLiteStorage instances and remove the database file
        std::error_code ec;
        if (std::filesystem::exists(dbName.c_str()))
        {
            std::filesystem::remove(dbName.c_str());
            if (ec)
            {
                std::cerr << "Error removing file: " << ec.message() << '\n';
            }
        }
    }

    void TearDown() override {}
};

namespace
{
    void StoreMessages(SQLiteStorage& storage, const nlohmann::json& messages, const std::string& tableName)
    {
        for (const auto& message : messages)
        {
            storage.Store(message, tableName);
        }
    }

    void RemoveMessages(SQLiteStorage& storage, size_t count, const std::string& tableName)
    {
        storage.RemoveMultiple(static_cast<int>(count), tableName);
    }
} // namespace

TEST_F(SQLiteStorageMultithreadedTest, MultithreadedStoreAndRetrieve)
{
    constexpr size_t messagesToStore = 100;

    SQLiteStorage storage1(dbName, m_vMessageTypeStrings);

    auto messages1 = nlohmann::json::array();
    auto messages2 = nlohmann::json::array();

    for (size_t i = 0; i < messagesToStore; i++)
    {
        messages1.push_back({{"key" + std::to_string(i), "value" + std::to_string(i)}});
        messages2.push_back({{std::to_string(i) + "key", std::to_string(i) + "value"}});
    }

    // Create threads for storing messages
    std::thread thread1(StoreMessages, std::ref(storage1), messages1, m_vMessageTypeStrings[0]);
    std::thread thread2(StoreMessages, std::ref(storage1), messages2, m_vMessageTypeStrings[0]);

    // Join the threads to ensure they complete
    thread1.join();
    thread2.join();

    EXPECT_EQ(storage1.GetElementCount(m_vMessageTypeStrings[0]), 2 * messagesToStore);

    std::thread thread3(RemoveMessages, std::ref(storage1), messagesToStore, m_vMessageTypeStrings[0]);
    std::thread thread4(RemoveMessages, std::ref(storage1), messagesToStore, m_vMessageTypeStrings[0]);

    // Join the threads to ensure they complete
    thread3.join();
    thread4.join();

    EXPECT_EQ(storage1.GetElementCount(m_vMessageTypeStrings[0]), 0);
}
