#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sca_policy_check.hpp>

#include <mock_filesystem_wrapper.hpp>

#include <filesystem>
#include <memory>

class CommandRuleEvaluatorTest : public ::testing::Test
{
protected:
    PolicyEvaluationContext m_ctx;
    std::unique_ptr<MockFileSystemWrapper> m_fsMock;
    MockFileSystemWrapper* m_rawFsMock = nullptr;
    std::function<std::pair<std::string, std::string>(const std::string&)> m_execMock;

    void SetUp() override
    {
        m_fsMock = std::make_unique<MockFileSystemWrapper>();
        m_rawFsMock = m_fsMock.get();
    }

    CommandRuleEvaluator CreateEvaluator()
    {
        return {m_ctx, std::move(m_fsMock), m_execMock};
    }
};

TEST_F(CommandRuleEvaluatorTest, EvaluationReturnsInvalidWhenNoPattern)
{
    m_ctx.rule = "some command";
    m_ctx.pattern = std::nullopt;

    m_execMock = [](const std::string&)
    {
        return std::make_pair("", "");
    };

    auto evaluator = CreateEvaluator();
    EXPECT_EQ(evaluator.Evaluate(), RuleResult::Invalid);
}

TEST_F(CommandRuleEvaluatorTest, CommandReturnsEmptyStringReturnsNotFound)
{
    m_ctx.rule = "some command";
    m_ctx.pattern = std::string("some pattern");

    m_execMock = [](const std::string&)
    {
        return std::make_pair("", "");
    };

    auto evaluator = CreateEvaluator();
    EXPECT_EQ(evaluator.Evaluate(), RuleResult::NotFound);
}

TEST_F(CommandRuleEvaluatorTest, CommandOutputMatchesLiteralPatternReturnsFound)
{
    m_ctx.rule = "some command";
    m_ctx.pattern = std::string("exact match");

    m_execMock = [](const std::string&)
    {
        return std::make_pair("exact match", "");
    };

    auto evaluator = CreateEvaluator();
    EXPECT_EQ(evaluator.Evaluate(), RuleResult::Found);
}

TEST_F(CommandRuleEvaluatorTest, CommandOutputDoesNotMatchLiteralPatternReturnsNotFound)
{
    m_ctx.rule = "some command";
    m_ctx.pattern = std::string("exact match");

    m_execMock = [](const std::string&)
    {
        return std::make_pair("something else", "");
    };

    auto evaluator = CreateEvaluator();
    EXPECT_EQ(evaluator.Evaluate(), RuleResult::NotFound);
}

TEST_F(CommandRuleEvaluatorTest, RegexPatternMatchesOutputReturnsFound)
{
    m_ctx.rule = "some command";
    m_ctx.pattern = std::string("r:success");

    m_execMock = [](const std::string&)
    {
        return std::make_pair("success", "");
    };

    auto evaluator = CreateEvaluator();
    EXPECT_EQ(evaluator.Evaluate(), RuleResult::Found);
}

TEST_F(CommandRuleEvaluatorTest, RegexPatternDoesNotMatchOutputReturnsNotFound)
{
    m_ctx.rule = "some command";
    m_ctx.pattern = std::string("r:fail");

    m_execMock = [](const std::string&)
    {
        return std::make_pair("ok", "");
    };

    auto evaluator = CreateEvaluator();
    EXPECT_EQ(evaluator.Evaluate(), RuleResult::NotFound);
}

TEST_F(CommandRuleEvaluatorTest, PatternGivenButCommandOutputIsEmptyReturnsNotFound)
{
    m_ctx.rule = "some command";
    m_ctx.pattern = std::string("r:foo");

    m_execMock = [](const std::string&)
    {
        return std::make_pair("", "");
    };

    auto evaluator = CreateEvaluator();
    EXPECT_EQ(evaluator.Evaluate(), RuleResult::NotFound);
}

TEST_F(CommandRuleEvaluatorTest, NumericPatternMatchesOutputReturnsFound)
{
    m_ctx.rule = "some command";
    m_ctx.pattern = std::string("n:\\d+ compare <= 50");

    m_execMock = [](const std::string&)
    {
        return std::make_pair("42", "");
    };

    auto evaluator = CreateEvaluator();
    EXPECT_EQ(evaluator.Evaluate(), RuleResult::Found);
}
