#include <gtest/gtest.h>

#include <sca.hpp>

#include <configuration_parser.hpp>
#include <dbsync.hpp>

#include "mocks/mockdbsync.hpp"

#include <memory>
#include <string>

class ScaTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_configurationParser = std::make_shared<configuration::ConfigurationParser>(std::string(R"(
            agent:
              retry_interval: 5
              verification_mode: none
              path.data: test_path
            events:
              batch_size: 1
        )"));

        m_sca = &SecurityConfigurationAssessment<MockDBSync>::Instance(m_configurationParser);
    }

    std::shared_ptr<configuration::ConfigurationParser> m_configurationParser = nullptr;
    SecurityConfigurationAssessment<MockDBSync>* m_sca = nullptr;
};

TEST_F(ScaTest, ConstructorSetsDbFilePath)
{
    const auto expectedDbFilePath =
        m_configurationParser->GetConfigOrDefault(config::DEFAULT_DATA_PATH, "agent", "path.data");
    EXPECT_EQ(MockDBSyncDbFilePath, expectedDbFilePath + "/sca.db");
}

TEST_F(ScaTest, SetPushMessageFunctionStoresCallback)
{
    constexpr int expectedReturnValue = 123;
    bool called = false;

    auto lambda = [&](Message) -> int // NOLINT(performance-unnecessary-value-param)
    {
        called = true;
        return expectedReturnValue;
    };

    m_sca->SetPushMessageFunction(lambda);

    const Message dummyMessage(MessageType::STATELESS, nlohmann::json {}, "dummyModule", "dummyType", "dummyMetaData");
    const int result = lambda(dummyMessage);

    EXPECT_TRUE(called);
    EXPECT_EQ(result, expectedReturnValue);
}

TEST_F(ScaTest, NameReturnsCorrectValue)
{
    EXPECT_EQ(m_sca->Name(), "SCA");
}

TEST_F(ScaTest, EnqueueTaskExecutesTask)
{
    bool taskExecuted = false;

    auto task = [&]() -> boost::asio::awaitable<void> // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    {
        taskExecuted = true;
        m_sca->Stop();
        co_return;
    };

    m_sca->EnqueueTask(task());
    m_sca->Start();
    EXPECT_TRUE(taskExecuted);
}
