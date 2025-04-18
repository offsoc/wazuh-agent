#pragma once

#include "isqliteWrapper.hpp"
#include <gmock/gmock.h>
#include <string>

class MockConnection : public SQLiteLegacy::IConnection
{
public:
    MockConnection() = default;
    virtual ~MockConnection() = default;
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(void, execute, (const std::string& query), (override));
    MOCK_METHOD(int64_t, changes, (), (const, override));
    MOCK_METHOD(const std::shared_ptr<sqlite3>&, db, (), (const, override));
};

class MockTransaction : public SQLiteLegacy::ITransaction
{
public:
    MOCK_METHOD(void, commit, (), (override));
    MOCK_METHOD(void, rollback, (), (override));
};

class MockColumn : public SQLiteLegacy::IColumn
{
public:
    MOCK_METHOD(bool, hasValue, (), (const, override));
    MOCK_METHOD(int32_t, type, (), (const, override));
    MOCK_METHOD(std::string, name, (), (const, override));
    MOCK_METHOD(int32_t, value, (const int32_t&), (const, override));
    MOCK_METHOD(uint64_t, value, (const uint64_t&), (const, override));
    MOCK_METHOD(int64_t, value, (const int64_t&), (const, override));
    MOCK_METHOD(std::string, value, (const std::string&), (const, override));
    MOCK_METHOD(double_t, value, (const double_t&), (const, override));
};

class MockStatement : public SQLiteLegacy::IStatement
{
public:
    MockStatement() = default;
    virtual ~MockStatement() = default;
    MOCK_METHOD(int32_t, step, (), (override));
    MOCK_METHOD(void, bind, (const int32_t index, const int32_t value), (override));
    MOCK_METHOD(void, bind, (const int32_t index, const uint64_t value), (override));
    MOCK_METHOD(void, bind, (const int32_t index, const int64_t value), (override));
    MOCK_METHOD(void, bind, (const int32_t index, const std::string& value), (override));
    MOCK_METHOD(void, bind, (const int32_t index, const double_t value), (override));
    MOCK_METHOD(void, bind, (const int32_t index), (override));
    MOCK_METHOD(std::string, expand, (), (override));

    MOCK_METHOD(std::unique_ptr<SQLiteLegacy::IColumn>, column, (const int32_t index), (override));

    MOCK_METHOD(void, reset, (), (override));
    MOCK_METHOD(int, columnsCount, (), (const, override));
};
