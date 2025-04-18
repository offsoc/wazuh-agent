#include "sqlite_dbengine.h"
#include "commonDefs.h"
#include "db_exception.h"
#include "isqliteWrapper.hpp"
#include "mapWrapperSafe.hpp"
#include "stringHelper.hpp"
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include <thread>

using namespace std::chrono_literals;
auto constexpr MAX_TRIES = 5;
auto constexpr COND_AND_SIZE = 5;

SQLiteDBEngine::SQLiteDBEngine(const std::shared_ptr<SQLiteLegacy::ISQLiteFactory>& sqliteFactory,
                               const std::string& path,
                               const std::string& tableStmtCreation,
                               const DbManagement dbManagement,
                               const std::vector<std::string>& upgradeStatements)
    : m_sqliteFactory(sqliteFactory)
{
    initialize(path, tableStmtCreation, dbManagement, upgradeStatements);
}

SQLiteDBEngine::~SQLiteDBEngine()
{
    const std::lock_guard<std::mutex> lock(m_stmtMutex);
    m_statementsCache.clear();

    if (m_transaction)
    {
        m_transaction->commit();
    }
}

void SQLiteDBEngine::setMaxRows(const std::string& table, const int64_t maxRows)
{
    if (0 != loadTableData(table))
    {
        const std::lock_guard<std::mutex> lock(m_maxRowsMutex);

        if (maxRows < 0)
        {
            throw dbengine_error {MIN_ROW_LIMIT_BELOW_ZERO};
        }
        else if (0 == maxRows)
        {
            m_maxRows.erase(table);
        }
        else
        {
            const auto stmt {getStatement("SELECT COUNT(*) FROM " + table + ";")};

            if (stmt->step() == SQLITE_ROW)
            {
                const auto currentRows {stmt->column(0)->value(int64_t {})};

                m_maxRows[table] = {maxRows, currentRows};
            }
            else
            {
                throw dbengine_error {SQL_STMT_ERROR};
            }
        }
    }
    else
    {
        throw dbengine_error {EMPTY_TABLE_METADATA};
    }
}

void SQLiteDBEngine::bulkInsert(const std::string& table, const nlohmann::json& data)
{
    if (0 != loadTableData(table))
    {
        const auto& tableFieldsMetaData {m_tableFields[table]};

        for (const auto& element : data)
        {
            InsertElement(table, tableFieldsMetaData, element);
        }
    }
    else
    {
        throw dbengine_error {EMPTY_TABLE_METADATA};
    }
}

void SQLiteDBEngine::refreshTableData(const nlohmann::json& data,
                                      const DbSync::ResultCallback& callback,
                                      std::unique_lock<std::shared_timed_mutex>& lock)
{
    const std::string table {data.at("table").is_string() ? data.at("table").get_ref<const std::string&>() : ""};

    if (createCopyTempTable(table))
    {
        bulkInsert(table + TEMP_TABLE_SUBFIX, data.at("data"));

        if (0 != loadTableData(table))
        {
            std::vector<std::string> primaryKeyList;

            if (getPrimaryKeysFromTable(table, primaryKeyList))
            {
                if (!removeNotExistsRows(table, primaryKeyList, callback, lock))
                {
                    std::cout << "Error during the delete rows update " << __LINE__ << " - " << __FILE__ << '\n';
                }

                if (!changeModifiedRows(table, primaryKeyList, callback, lock))
                {
                    std::cout << "Error during the change of modified rows " << __LINE__ << " - " << __FILE__ << '\n';
                }

                if (!insertNewRows(table, primaryKeyList, callback, lock))
                {
                    std::cout << "Error during the insert rows update " << __LINE__ << " - " << __FILE__ << '\n';
                }
            }
        }
        else
        {
            throw dbengine_error {EMPTY_TABLE_METADATA};
        }
    }
}

void SQLiteDBEngine::syncTableRowData(const nlohmann::json& jsInput,
                                      const DbSync::ResultCallback& callback,
                                      const bool inTransaction,
                                      ILocking& lock)
{
    const auto& table {jsInput.at("table")};
    const auto& data {jsInput.at("data")};

    auto it {jsInput.find("options")};
    auto returnOldData {false};
    nlohmann::json ignoredColumns {};

    if (jsInput.end() != it)
    {
        auto itOldData {it->find("return_old_data")};

        if (it->end() != itOldData)
        {
            returnOldData = itOldData->is_boolean() ? itOldData.value().get<bool>() : returnOldData;
        }

        auto itIgnoredFields {it->find("ignore")};

        if (it->end() != itIgnoredFields)
        {
            ignoredColumns = itIgnoredFields->is_array() ? itIgnoredFields.value() : ignoredColumns;
        }
    }

    static const auto getDataToUpdate {[](const std::vector<std::string>& primaryKeyList,
                                          const nlohmann::json& result,
                                          const nlohmann::json& dataParam,
                                          const bool inTransactionParam)
                                       {
                                           nlohmann::json ret;

                                           if (inTransactionParam)
                                           {
                                               // No changes detected, only update the status field to avoid row
                                               // deletion during the txn close.
                                               if (result.empty())
                                               {
                                                   std::for_each(primaryKeyList.begin(),
                                                                 primaryKeyList.end(),
                                                                 [&dataParam, &ret](const std::string& pKey)
                                                                 {
                                                                     if (dataParam.find(pKey) != dataParam.end())
                                                                     {
                                                                         ret[pKey] = dataParam[pKey];
                                                                     }
                                                                 });
                                               }
                                               else // Changes detected, update the row with the new values.
                                               {
                                                   ret = result;
                                               }

                                               ret[STATUS_FIELD_NAME] = 1;
                                           }
                                           else if (!result.empty())
                                           {
                                               ret = result;
                                           }

                                           return ret;
                                       }};
    std::vector<std::string> primaryKeyList;

    if (0 != loadTableData(table))
    {
        if (getPrimaryKeysFromTable(table, primaryKeyList))
        {
            for (const auto& entry : data)
            {
                nlohmann::json updated;
                nlohmann::json oldData;
                const bool diffExist {getRowDiff(primaryKeyList, ignoredColumns, table, entry, updated, oldData)};

                if (diffExist)
                {
                    const auto& jsDataToUpdate {getDataToUpdate(primaryKeyList, updated, entry, inTransaction)};

                    if (!jsDataToUpdate.empty())
                    {
                        updateSingleRow(table, jsDataToUpdate);

                        if (callback && !updated.empty())
                        {

                            lock.unlock();

                            if (returnOldData)
                            {
                                nlohmann::json diff;
                                diff["old"] = oldData;
                                diff["new"] = updated;
                                callback(MODIFIED, diff);
                            }
                            else
                            {
                                callback(MODIFIED, updated);
                            }

                            lock.lock();
                        }
                    }
                }
                else
                {
                    InsertElement(table,
                                  m_tableFields[table],
                                  entry,
                                  [&]()
                                  {
                                      if (callback)
                                      {
                                          lock.unlock();
                                          callback(INSERTED, entry);
                                          lock.lock();
                                      }
                                  });
                }
            }
        }
    }
    else
    {
        throw dbengine_error {EMPTY_TABLE_METADATA};
    }
}

void SQLiteDBEngine::initializeStatusField(const nlohmann::json& tableNames)
{
    for (const auto& tableValue : tableNames)
    {
        const auto table {tableValue.get<std::string>()};

        if (0 != loadTableData(table))
        {
            const auto& fields {m_tableFields[table]};
            const auto& it {std::find_if(fields.begin(),
                                         fields.end(),
                                         [](const ColumnData& column)
                                         { return 0 == std::get<Name>(column).compare(STATUS_FIELD_NAME); })};

            if (fields.end() == it)
            {
                m_tableFields.erase(table);
                const auto stmtAdd {getStatement("ALTER TABLE " + table + " ADD COLUMN " + STATUS_FIELD_NAME + " " +
                                                 STATUS_FIELD_TYPE + " DEFAULT 1;")};

                if (SQLITE_ERROR == stmtAdd->step())
                {
                    throw dbengine_error {STEP_ERROR_UPDATE_STATUS_FIELD};
                }
            }

            const auto& stmtInit {getStatement("UPDATE " + table + " SET " + STATUS_FIELD_NAME + "=0;")};

            if (SQLITE_ERROR == stmtInit->step())
            {
                throw dbengine_error {STEP_ERROR_ADD_STATUS_FIELD};
            }
        }
        else
        {
            throw dbengine_error {EMPTY_TABLE_METADATA};
        }
    }
}

void SQLiteDBEngine::deleteRowsByStatusField(const nlohmann::json& tableNames)
{
    for (const auto& tableValue : tableNames)
    {
        const auto table {tableValue.get<std::string>()};

        if (0 != loadTableData(table))
        {
            const auto stmt {getStatement("DELETE FROM " + table + " WHERE " + STATUS_FIELD_NAME + "=0;")};

            if (SQLITE_ERROR == stmt->step())
            {
                throw dbengine_error {STEP_ERROR_DELETE_STATUS_FIELD};
            }

            updateTableRowCounter(table, m_sqliteConnection->changes() * -1ll);
        }
        else
        {
            throw dbengine_error {EMPTY_TABLE_METADATA};
        }
    }
}

void SQLiteDBEngine::returnRowsMarkedForDelete(const nlohmann::json& tableNames,
                                               const DbSync::ResultCallback& callback,
                                               std::unique_lock<std::shared_timed_mutex>& lock)
{
    if (m_transaction)
    {
        m_transaction->commit();
    }
    m_transaction = m_sqliteFactory->createTransaction(m_sqliteConnection);

    for (const auto& tableValue : tableNames)
    {
        const auto& table {tableValue.get<std::string>()};

        if (0 != loadTableData(table))
        {
            auto tableFields {m_tableFields[table]};
            const auto stmt {getStatement(getSelectAllQuery(table, tableFields))};

            while (SQLITE_ROW == stmt->step())
            {
                Row registerFields;
                auto index {0};

                for (const auto& field : tableFields)
                {
                    if (!std::get<TableHeader::TXNStatusField>(field))
                    {
                        getTableData(stmt,
                                     index,
                                     std::get<TableHeader::Type>(field),
                                     std::get<TableHeader::Name>(field),
                                     registerFields);
                    }

                    ++index;
                }

                nlohmann::json object {};

                for (const auto& value : registerFields)
                {
                    getFieldValueFromTuple(value, object);
                }

                lock.unlock();
                callback(ReturnTypeCallback::DELETED, object);
                lock.lock();
            }
        }
        else
        {
            throw dbengine_error {EMPTY_TABLE_METADATA};
        }
    }
}

void SQLiteDBEngine::selectData(const std::string& table,
                                const nlohmann::json& query,
                                const DbSync::ResultCallback& callback,
                                std::unique_lock<std::shared_timed_mutex>& lock)
{
    if (0 != loadTableData(table))
    {
        const auto& stmt {m_sqliteFactory->createStatement(m_sqliteConnection, buildSelectQuery(table, query))};

        while (SQLITE_ROW == stmt->step())
        {
            nlohmann::json object;

            for (int i = 0; i < stmt->columnsCount(); ++i)
            {
                const auto& column {stmt->column(i)};
                const auto& name {column->name()};

                if (column->hasValue() && name != STATUS_FIELD_NAME)
                {
                    switch (column->type())
                    {
                        case SQLITE_TEXT: object[name] = column->value(std::string {}); break;

                        case SQLITE_INTEGER: object[name] = column->value(int64_t {}); break;

                        case SQLITE_FLOAT: object[name] = column->value(double_t {}); break;

                        default: throw dbengine_error {INVALID_COLUMN_TYPE};
                    }
                }
            }

            if (callback && !object.empty())
            {
                lock.unlock();
                callback(SELECTED, object);
                lock.lock();
            }
        }
    }
    else
    {
        throw dbengine_error {EMPTY_TABLE_METADATA};
    }
}

void SQLiteDBEngine::deleteTableRowsData(const std::string& table, const nlohmann::json& jsDeletionData)
{
    if (0 != loadTableData(table))
    {
        const auto& itData {jsDeletionData.find("data")};
        const auto& itFilter {jsDeletionData.find("where_filter_opt")};

        if (itData != jsDeletionData.end() && !itData->empty())
        {
            // Deletion via primary keys on "data" json field.
            deleteRowsbyPK(table, itData.value());
        }
        else if (itFilter != jsDeletionData.end() && !itFilter->get<std::string>().empty())
        {
            // Deletion via condition on "where_filter_opt" json field.
            m_sqliteConnection->execute("DELETE FROM " + table + " WHERE " + itFilter->get<std::string>());
            updateTableRowCounter(table, m_sqliteConnection->changes() * -1ll);
        }
        else
        {
            throw dbengine_error {INVALID_DELETE_INFO};
        }
    }
    else
    {
        throw dbengine_error {EMPTY_TABLE_METADATA};
    }
}

void SQLiteDBEngine::addTableRelationship(const nlohmann::json& data)
{
    const auto baseTable {data.at("base_table").get<std::string>()};

    if (0 != loadTableData(baseTable))
    {
        std::vector<std::string> primaryKeys;

        if (getPrimaryKeysFromTable(baseTable, primaryKeys))
        {
            m_sqliteConnection->execute(buildDeleteRelationTrigger(data, baseTable));
            m_sqliteConnection->execute(buildUpdateRelationTrigger(data, baseTable, primaryKeys));
        }
    }
    else
    {
        throw dbengine_error {EMPTY_TABLE_METADATA};
    }
}

///
/// Private functions section
///

void SQLiteDBEngine::initialize(const std::string& path,
                                const std::string& tableStmtCreation,
                                const DbManagement dbManagement,
                                const std::vector<std::string>& upgradeStatements)
{
    if (path.empty())
    {
        throw dbengine_error {EMPTY_DATABASE_PATH};
    }

    auto currentDbsyncVersion = upgradeStatements.size() + 1;

    auto reCreateDbLambda = [&]()
    {
        if (!cleanDB(path))
        {
            throw dbengine_error {DELETE_OLD_DB_ERROR};
        }

        m_sqliteConnection = m_sqliteFactory->createConnection(path);
        const auto createDBQueryList {Utils::split(tableStmtCreation, ';')};
        m_sqliteConnection->execute("PRAGMA temp_store = memory;");
        m_sqliteConnection->execute("PRAGMA journal_mode = truncate;");
        m_sqliteConnection->execute("PRAGMA synchronous = OFF;");
        m_sqliteConnection->execute("PRAGMA user_version = " + std::to_string(currentDbsyncVersion) + ";");

        for (const auto& query : createDBQueryList)
        {
            const auto stmt {getStatement(query)};

            if (SQLITE_DONE != stmt->step())
            {
                throw dbengine_error {STEP_ERROR_CREATE_STMT};
            }
        }

        m_transaction = m_sqliteFactory->createTransaction(m_sqliteConnection);
    };

    size_t dbVersion = 0;

    if (DbManagement::PERSISTENT == dbManagement)
    {
        m_sqliteConnection = m_sqliteFactory->createConnection(path);
        dbVersion = getDbVersion();

        if (0 == dbVersion)
        {
            m_sqliteConnection.reset();
            reCreateDbLambda();
        }

        else if (dbVersion < currentDbsyncVersion)
        {
            for (size_t i = dbVersion - 1; i < upgradeStatements.size(); ++i)
            {
                auto transaction = m_sqliteFactory->createTransaction(m_sqliteConnection);
                const auto stmt {m_sqliteFactory->createStatement(m_sqliteConnection, upgradeStatements[i])};

                if (SQLITE_DONE != stmt->step())
                {
                    throw dbengine_error {STEP_ERROR_UPDATE_STMT};
                }

                transaction->commit();
                m_sqliteConnection->execute("PRAGMA user_version = " + std::to_string(i + 2) + ";");
            }

            m_transaction = m_sqliteFactory->createTransaction(m_sqliteConnection);
        }
    }
    else if (DbManagement::VOLATILE == dbManagement)
    {
        reCreateDbLambda();
    }
}

bool SQLiteDBEngine::cleanDB(const std::string& path)
{
    auto ret {true};
    auto isRemoved {0};

    if (path.compare(":memory") != 0)
    {
        if (std::ifstream(path))
        {
            isRemoved = std::remove(path.c_str());

            for (uint8_t amountTries = 0; amountTries < MAX_TRIES && isRemoved; amountTries++)
            {
                std::this_thread::sleep_for(1s); //< Sleep for 1s
                std::cerr << "Sleep for 1s and try to delete database again.\n";
                isRemoved = std::remove(path.c_str());
            }

            if (isRemoved)
            {
                ret = false;
            }
        }
    }

    return ret;
}

size_t SQLiteDBEngine::getDbVersion()
{
    const auto stmt {m_sqliteFactory->createStatement(m_sqliteConnection, "PRAGMA user_version;")};

    size_t version {0};

    if (SQLITE_ROW == stmt->step())
    {
        version = stmt->column(0)->value(uint64_t {});
    }

    return version;
}

void SQLiteDBEngine::InsertElement(const std::string& table,
                                   const TableColumns& tableColumns,
                                   const nlohmann::json& element,
                                   const std::function<void()>& callback)
{
    const auto stmt {getStatement(buildInsertDataSqlQuery(table, element))};
    int32_t index {1l};

    for (const auto& field : tableColumns)
    {
        if (bindJsonData(stmt, field, element, index))
        {
            ++index;
        }
    }

    updateTableRowCounter(table, 1ll);

    if (SQLITE_ERROR == stmt->step())
    {
        updateTableRowCounter(table, -1ll);
        throw dbengine_error {BIND_FIELDS_DOES_NOT_MATCH};
    }

    if (callback)
    {
        callback();
    }
}

size_t SQLiteDBEngine::loadTableData(const std::string& table)
{
    size_t fieldsNumber {0ull};
    const auto& tableFields {m_tableFields[table]};

    if (tableFields.empty())
    {
        if (loadFieldData(table))
        {
            fieldsNumber = m_tableFields[table].size();
        }
    }
    else
    {
        fieldsNumber = tableFields.size();
    }

    return fieldsNumber;
}

std::string SQLiteDBEngine::buildInsertDataSqlQuery(const std::string& table, const nlohmann::json& data)
{
    //
    // The INSERT statement will be as the following:
    //  INSERT INTO table (column1, column2, ...) VALUES (valueColumn1, valueColumn2, ...);
    //
    std::string sql {"INSERT INTO " + table + " ("};
    std::string binds {") VALUES ("};

    const auto tableFields {m_tableFields[table]};

    if (!tableFields.empty())
    {
        for (const auto& field : tableFields)
        {
            const auto& fieldName {std::get<TableHeader::Name>(field)};

            if (data.empty() || data.find(fieldName) != data.end())
            {
                sql.append(fieldName + ",");
                binds.append("?,");
            }
        }

        // Remove extra "," for both strings
        binds = binds.substr(0, binds.size() - 1);
        sql = sql.substr(0, sql.size() - 1);
        // Finish the statement
        binds.append(");");
        // Complete the statement
        sql.append(binds);
    }
    else
    {
        throw dbengine_error {SQL_STMT_ERROR};
    }

    return sql;
}

bool SQLiteDBEngine::loadFieldData(const std::string& table)
{
    constexpr auto WITHOUT_ROW_ID_POSITION {5};
    const auto ret {!table.empty()};
    const std::string sql {"PRAGMA table_info(" + table + ");"};

    if (ret)
    {
        TableColumns fieldList;
        auto stmt {m_sqliteFactory->createStatement(m_sqliteConnection, sql)};

        while (SQLITE_ROW == stmt->step())
        {
            const auto& fieldName {stmt->column(1)->value(std::string {})};
            fieldList.emplace_back(
                std::make_tuple(stmt->column(0)->value(int32_t {}),
                                fieldName,
                                columnTypeName(stmt->column(2)->value(std::string {})),
                                0 != stmt->column(WITHOUT_ROW_ID_POSITION)->value(int32_t {}),
                                InternalColumnNames.end() !=
                                    std::find(InternalColumnNames.begin(), InternalColumnNames.end(), fieldName)));
        }

        m_tableFields.insert(table, fieldList);
    }

    return ret;
}

ColumnType SQLiteDBEngine::columnTypeName(const std::string& type)
{
    ColumnType retVal {Unknown};
    const auto& hiddenIt {type.find(" HIDDEN")};
    const auto& it {hiddenIt == std::string::npos ? ColumnTypeNames.find(type)
                                                  : ColumnTypeNames.find(type.substr(0, hiddenIt))};

    if (ColumnTypeNames.end() != it)
    {
        retVal = it->second;
    }

    return retVal;
}

bool SQLiteDBEngine::bindJsonData(const std::shared_ptr<SQLiteLegacy::IStatement> stmt,
                                  const ColumnData& cd,
                                  const nlohmann::json::value_type& valueType,
                                  const int32_t cid)
{
    bool retVal {true};
    const auto type {std::get<TableHeader::Type>(cd)};
    const auto name {std::get<TableHeader::Name>(cd)};
    const auto isPrimaryKey {std::get<TableHeader::PK>(cd)};
    const auto& it {valueType.find(name)};

    if (valueType.end() != it)
    {
        const auto& jsData {*it};

        if (jsData.is_null())
        {
            if (!isPrimaryKey)
            {
                stmt->bind(cid);
            }
            else
            {
                throw dbengine_error {INVALID_DATA_BIND};
            }
        }
        else if (ColumnType::BigInt == type)
        {
            const int64_t value {jsData.is_number() ? jsData.get<int64_t>()
                                 : jsData.is_string() && !jsData.get_ref<const std::string&>().empty()
                                     ? std::stoll(jsData.get_ref<const std::string&>())
                                     : 0};
            stmt->bind(cid, value);
        }
        else if (ColumnType::UnsignedBigInt == type)
        {
            const uint64_t value {jsData.is_number_unsigned() ? jsData.get<uint64_t>()
                                  : jsData.is_string() && !jsData.get_ref<const std::string&>().empty()
                                      ? std::stoull(jsData.get_ref<const std::string&>())
                                      : 0};
            stmt->bind(cid, value);
        }
        else if (ColumnType::Integer == type)
        {
            const int32_t value {jsData.is_number() ? jsData.get<int32_t>()
                                 : jsData.is_string() && !jsData.get_ref<const std::string&>().empty()
                                     ? std::stoi(jsData.get_ref<const std::string&>())
                                     : 0};
            stmt->bind(cid, value);
        }
        else if (ColumnType::Text == type)
        {
            const std::string value {jsData.is_string() ? jsData.get_ref<const std::string&>() : ""};
            stmt->bind(cid, value);
        }
        else if (ColumnType::Double == type)
        {
            const double_t value {jsData.is_number_float() ? jsData.get<double>()
                                  : jsData.is_string() && !jsData.get_ref<const std::string&>().empty()
                                      ? std::stod(jsData.get_ref<const std::string&>())
                                      : .0f};
            stmt->bind(cid, value);
        }
        else
        {
            throw dbengine_error {INVALID_COLUMN_TYPE};
        }
    }
    else
    {
        retVal = false;
    }

    return retVal;
}

bool SQLiteDBEngine::createCopyTempTable(const std::string& table)
{
    auto ret {false};
    std::string queryResult;
    deleteTempTable(table);

    if (getTableCreateQuery(table, queryResult))
    {
        if (Utils::replaceAll(
                queryResult, "CREATE TABLE " + table, "CREATE TEMP TABLE IF NOT EXISTS " + table + "_TEMP"))
        {
            const auto stmt {getStatement(queryResult)};
            ret = SQLITE_DONE == stmt->step();
        }
    }

    return ret;
}

void SQLiteDBEngine::deleteTempTable(const std::string& table)
{
    try
    {
        m_sqliteConnection->execute("DELETE FROM " + table + TEMP_TABLE_SUBFIX + ";");
    }
    // if the table doesn't exist we don't care.
    catch (const std::exception& ex)
    {
        (void)ex;
    }
}

bool SQLiteDBEngine::getTableCreateQuery(const std::string& table, std::string& resultQuery)
{
    auto ret {false};
    const std::string sql {"SELECT sql FROM sqlite_master WHERE type='table' AND name=?;"};

    if (!table.empty())
    {
        const auto stmt {getStatement(sql)};
        stmt->bind(1, table);

        while (SQLITE_ROW == stmt->step())
        {
            resultQuery.append(stmt->column(0)->value(std::string {}));
            resultQuery.append(";");
            ret = true;
        }
    }

    return ret;
}

bool SQLiteDBEngine::removeNotExistsRows(const std::string& table,
                                         const std::vector<std::string>& primaryKeyList,
                                         const DbSync::ResultCallback& callback,
                                         std::unique_lock<std::shared_timed_mutex>& lock)
{
    auto ret {true};
    std::vector<Row> rowKeysValue;

    if (getPKListLeftOnly(table, table + TEMP_TABLE_SUBFIX, primaryKeyList, rowKeysValue))
    {
        if (deleteRows(table, primaryKeyList, rowKeysValue))
        {
            for (const auto& row : rowKeysValue)
            {
                nlohmann::json object;

                for (const auto& value : row)
                {
                    getFieldValueFromTuple(value, object);
                }

                if (callback)
                {
                    lock.unlock();
                    callback(ReturnTypeCallback::DELETED, object);
                    lock.lock();
                }
            }
        }
        else
        {
            ret = false;
        }
    }

    return ret;
}

bool SQLiteDBEngine::getPrimaryKeysFromTable(const std::string& table, std::vector<std::string>& primaryKeyList)
{
    auto retVal {false};
    const auto tableFields {m_tableFields[table]};

    for (const auto& value : tableFields)
    {
        if (std::get<TableHeader::PK>(value) == true)
        {
            primaryKeyList.push_back(std::get<TableHeader::Name>(value));
        }

        retVal = true;
    }

    return retVal;
}

void SQLiteDBEngine::getTableData(const std::shared_ptr<SQLiteLegacy::IStatement> stmt,
                                  const int32_t index,
                                  const ColumnType& type,
                                  const std::string& fieldName,
                                  Row& row)
{
    if (!stmt->column(index)->hasValue())
    {
        row[fieldName] = std::make_tuple(type, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
        return;
    }

    if (ColumnType::BigInt == type)
    {
        row[fieldName] = std::make_tuple(
            type, std::nullopt, std::nullopt, stmt->column(index)->value(int64_t {}), std::nullopt, std::nullopt);
    }
    else if (ColumnType::UnsignedBigInt == type)
    {
        row[fieldName] = std::make_tuple(
            type, std::nullopt, std::nullopt, std::nullopt, stmt->column(index)->value(uint64_t {}), std::nullopt);
    }
    else if (ColumnType::Integer == type)
    {
        row[fieldName] = std::make_tuple(
            type, std::nullopt, stmt->column(index)->value(int32_t {}), std::nullopt, std::nullopt, std::nullopt);
    }
    else if (ColumnType::Text == type)
    {
        row[fieldName] = std::make_tuple(
            type, stmt->column(index)->value(std::string {}), std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    }
    else if (ColumnType::Double == type)
    {
        row[fieldName] = std::make_tuple(
            type, std::nullopt, std::nullopt, std::nullopt, std::nullopt, stmt->column(index)->value(double_t {}));
    }
    else
    {
        throw dbengine_error {INVALID_COLUMN_TYPE};
    }
}

bool SQLiteDBEngine::getLeftOnly(const std::string& t1,
                                 const std::string& t2,
                                 const std::vector<std::string>& primaryKeyList,
                                 std::vector<Row>& returnRows)
{
    auto ret {false};
    const std::string query {buildLeftOnlyQuery(t1, t2, primaryKeyList)};

    if (!t1.empty() && !query.empty())
    {
        const auto stmt {getStatement(query)};
        const auto tableFields {m_tableFields[t1]};

        while (SQLITE_ROW == stmt->step())
        {
            Row registerFields;

            for (const auto& field : tableFields)
            {
                getTableData(stmt,
                             std::get<TableHeader::CID>(field),
                             std::get<TableHeader::Type>(field),
                             std::get<TableHeader::Name>(field),
                             registerFields);
            }

            returnRows.push_back(std::move(registerFields));
        }

        ret = true;
    }

    return ret;
}

bool SQLiteDBEngine::getPKListLeftOnly(const std::string& t1,
                                       const std::string& t2,
                                       const std::vector<std::string>& primaryKeyList,
                                       std::vector<Row>& returnRows)
{
    auto ret {false};
    const std::string sql {buildLeftOnlyQuery(t1, t2, primaryKeyList, true)};

    if (!t1.empty() && !sql.empty())
    {
        const auto stmt {getStatement(sql)};
        const auto tableFields {m_tableFields[t1]};

        while (SQLITE_ROW == stmt->step())
        {
            Row registerFields;

            for (const auto& pkValue : primaryKeyList)
            {
                auto index {0ull};
                const auto& it {std::find_if(tableFields.begin(),
                                             tableFields.end(),
                                             [&pkValue](const ColumnData& columnData)
                                             { return std::get<TableHeader::Name>(columnData) == pkValue; })};

                if (tableFields.end() != it)
                {
                    getTableData(stmt,
                                 static_cast<int32_t>(index),
                                 std::get<TableHeader::Type>(*it),
                                 std::get<TableHeader::Name>(*it),
                                 registerFields);
                }

                ++index;
            }

            returnRows.push_back(std::move(registerFields));
        }

        ret = true;
    }

    return ret;
}

std::string SQLiteDBEngine::buildDeleteBulkDataSqlQuery(const std::string& table,
                                                        const std::vector<std::string>& primaryKeyList)
{
    std::string sql {"DELETE FROM "};
    sql.append(table);
    sql.append(" WHERE ");

    if (!primaryKeyList.empty())
    {
        for (const auto& value : primaryKeyList)
        {
            sql.append(value);
            sql.append("=? AND ");
        }

        sql = sql.substr(0, sql.size() - COND_AND_SIZE); // Remove the last " AND "
        sql.append(";");
    }
    else
    {
        throw dbengine_error {SQL_STMT_ERROR};
    }

    return sql;
}

bool SQLiteDBEngine::deleteRows(const std::string& table,
                                const std::vector<std::string>& primaryKeyList,
                                const std::vector<Row>& rowsToRemove)
{
    auto ret {false};
    const auto sql {buildDeleteBulkDataSqlQuery(table, primaryKeyList)};

    if (!sql.empty())
    {
        const auto stmt {getStatement(sql)};

        for (const auto& row : rowsToRemove)
        {
            int32_t index {1l};

            for (const auto& value : primaryKeyList)
            {
                bindFieldData(stmt, index, row.at(value));
                ++index;
            }

            if (SQLITE_ERROR == stmt->step())
            {
                throw dbengine_error {BIND_FIELDS_DOES_NOT_MATCH};
            }

            updateTableRowCounter(table, m_sqliteConnection->changes() * -1ll);

            stmt->reset();
        }

        ret = true;
    }
    else
    {
        throw dbengine_error {SQL_STMT_ERROR};
    }

    return ret;
}

void SQLiteDBEngine::deleteRowsbyPK(const std::string& table, const nlohmann::json& data)
{
    std::vector<std::string> primaryKeyList;

    if (getPrimaryKeysFromTable(table, primaryKeyList))
    {
        const auto& tableFields {m_tableFields[table]};
        const auto stmt {getStatement(buildDeleteBulkDataSqlQuery(table, primaryKeyList))};

        for (const auto& jsRow : data)
        {
            int32_t index {1l};

            for (const auto& pkValue : primaryKeyList)
            {
                const auto& it {std::find_if(tableFields.begin(),
                                             tableFields.end(),
                                             [&pkValue](const ColumnData& column)
                                             { return 0 == std::get<Name>(column).compare(pkValue); })};

                if (it != tableFields.end())
                {
                    if (bindJsonData(stmt, *it, jsRow, index))
                    {
                        ++index;
                    }
                }
            }

            if (SQLITE_ERROR == stmt->step())
            {
                throw dbengine_error {BIND_FIELDS_DOES_NOT_MATCH};
            }

            updateTableRowCounter(table, m_sqliteConnection->changes() * -1ll);

            stmt->reset();
        }
    }
}

void SQLiteDBEngine::bindFieldData(const std::shared_ptr<SQLiteLegacy::IStatement> stmt,
                                   const int32_t index,
                                   const TableField& fieldData)
{
    const auto type {std::get<GenericTupleIndex::GenType>(fieldData)};

    if (ColumnType::BigInt == type)
    {
        const auto value {std::get<GenericTupleIndex::GenBigInt>(fieldData)};
        if (value.has_value())
        {
            stmt->bind(index, value.value());
        }
        else
        {
            stmt->bind(index);
        }
    }
    else if (ColumnType::UnsignedBigInt == type)
    {
        const auto value {std::get<GenericTupleIndex::GenUnsignedBigInt>(fieldData)};
        if (value.has_value())
        {
            stmt->bind(index, value.value());
        }
        else
        {
            stmt->bind(index);
        }
    }
    else if (ColumnType::Integer == type)
    {
        const auto value {std::get<GenericTupleIndex::GenInteger>(fieldData)};
        if (value.has_value())
        {
            stmt->bind(index, value.value());
        }
        else
        {
            stmt->bind(index);
        }
    }
    else if (ColumnType::Text == type)
    {
        const auto value {std::get<GenericTupleIndex::GenString>(fieldData)};
        if (value.has_value())
        {
            stmt->bind(index, value.value());
        }
        else
        {
            stmt->bind(index);
        }
    }
    else if (ColumnType::Double == type)
    {
        const auto value {std::get<GenericTupleIndex::GenDouble>(fieldData)};
        if (value.has_value())
        {
            stmt->bind(index, value.value());
        }
        else
        {
            stmt->bind(index);
        }
    }
    else
    {
        throw dbengine_error {INVALID_DATA_BIND};
    }
}

std::string SQLiteDBEngine::buildSelectQuery(const std::string& table, const nlohmann::json& jsQuery)
{
    const auto& columns {jsQuery.at("column_list")};
    const auto& itFilter {jsQuery.find("row_filter")};
    /*optional fields*/
    const auto& itDistinct {jsQuery.find("distinct_opt")};
    const auto& itOrderBy {jsQuery.find("order_by_opt")};
    const auto& itCount {jsQuery.find("count_opt")};

    std::string sql {"SELECT "};

    if (itDistinct != jsQuery.end() && itDistinct->get<bool>())
    {
        sql += "DISTINCT ";
    }

    for (const auto& column : columns)
    {
        sql += column.get_ref<const std::string&>();
        sql += ",";
    }

    sql = sql.substr(0, sql.size() - 1);

    sql += " FROM " + table;

    if (itFilter != jsQuery.end() && !itFilter->get<std::string>().empty())
    {
        sql += " ";
        sql += itFilter->get<std::string>();
    }

    if (itOrderBy != jsQuery.end() && !itOrderBy->get<std::string>().empty())
    {
        sql += " ORDER BY " + itOrderBy->get<std::string>();
    }

    if (itCount != jsQuery.end())
    {
        const unsigned int limit {*itCount};
        sql += " LIMIT " + std::to_string(limit);
    }

    sql += ";";
    return sql;
}

std::string SQLiteDBEngine::buildLeftOnlyQuery(const std::string& t1,
                                               const std::string& t2,
                                               const std::vector<std::string>& primaryKeyList,
                                               const bool returnOnlyPKFields)
{
    std::string fieldsList;
    std::string onMatchList;
    std::string nullFilterList;

    for (const auto& value : primaryKeyList)
    {
        if (returnOnlyPKFields)
        {
            fieldsList.append("t1." + value + ",");
        }

        onMatchList.append("t1." + value);
        onMatchList.append("= t2." + value + " AND ");
        nullFilterList.append("t2." + value + " IS NULL AND ");
    }

    if (returnOnlyPKFields)
    {
        fieldsList = fieldsList.substr(0, fieldsList.size() - 1);
    }
    else
    {
        fieldsList.append("*");
    }

    onMatchList = onMatchList.substr(0, onMatchList.size() - COND_AND_SIZE);          // Remove the last " AND "
    nullFilterList = nullFilterList.substr(0, nullFilterList.size() - COND_AND_SIZE); // Remove the last " AND "

    return "SELECT " + fieldsList + " FROM " + t1 + " t1 LEFT JOIN " + t2 + " t2 ON " + onMatchList + " WHERE " +
           nullFilterList + ";";
}

bool SQLiteDBEngine::getRowDiff(const std::vector<std::string>& primaryKeyList,
                                const nlohmann::json& ignoredColumns,
                                const std::string& table,
                                const nlohmann::json& data,
                                nlohmann::json& updatedData,
                                nlohmann::json& oldData)
{
    bool diffExist {false};
    bool isModified {false};
    const auto stmt {getStatement(buildSelectMatchingPKsSqlQuery(table, primaryKeyList))};

    const auto& tableFields {m_tableFields[table]};
    int32_t index {1l};

    // Always include primary keys
    for (const auto& pkValue : primaryKeyList)
    {
        const auto& it {std::find_if(tableFields.begin(),
                                     tableFields.end(),
                                     [&pkValue](const ColumnData& column)
                                     { return 0 == std::get<Name>(column).compare(pkValue); })};

        if (it != tableFields.end())
        {
            updatedData[pkValue] = data.at(pkValue);
            oldData[pkValue] = data.at(pkValue);
            bindJsonData(stmt, *it, data, index);
            ++index;
        }
    }

    diffExist = SQLITE_ROW == stmt->step();

    if (diffExist)
    {
        // The row exists, so let's generate the diff
        Row registryFields;

        for (const auto& field : tableFields)
        {
            getTableData(stmt,
                         std::get<TableHeader::CID>(field),
                         std::get<TableHeader::Type>(field),
                         std::get<TableHeader::Name>(field),
                         registryFields);
        }

        if (!registryFields.empty())
        {
            for (const auto& value : registryFields)
            {
                nlohmann::json object;
                getFieldValueFromTuple(value, object);
                const auto& it {data.find(value.first)};

                if (data.end() != it)
                {
                    // Only compare if not in ignore set
                    if (*it != object.at(value.first))
                    {
                        // Diff found
                        isModified = true;
                        oldData[value.first] = object[value.first];
                    }

                    updatedData[value.first] = *it;
                }
            }
        }
    }

    // If the row is not modified, we clear the result to update the status field value only.
    if (!isModified)
    {
        updatedData.clear();
        oldData.clear();
    }
    else
    {
        if (!ignoredColumns.empty())
        {
            auto haveDiffOnNonIgnored {
                [&ignoredColumns, primaryKeyList](const nlohmann::json& rowToBeUpdated) -> bool
                {
                    bool haveDiff {false};

                    for (const auto& fieldToBeUpdated : rowToBeUpdated.items())
                    {
                        if (std::find(ignoredColumns.begin(), ignoredColumns.end(), fieldToBeUpdated.key()) ==
                            ignoredColumns.end())
                        {
                            if (std::find(primaryKeyList.begin(), primaryKeyList.end(), fieldToBeUpdated.key()) ==
                                primaryKeyList.end())
                            {
                                haveDiff = true;
                                break;
                            }
                        }
                    }

                    return haveDiff;
                }};

            if (!haveDiffOnNonIgnored(oldData))
            {
                updatedData.clear();
                oldData.clear();
            }
        }
    }

    return diffExist;
}

bool SQLiteDBEngine::insertNewRows(const std::string& table,
                                   const std::vector<std::string>& primaryKeyList,
                                   const DbSync::ResultCallback& callback,
                                   std::unique_lock<std::shared_timed_mutex>& lock)
{
    auto ret {true};
    std::vector<Row> rowValues;

    if (getLeftOnly(table + TEMP_TABLE_SUBFIX, table, primaryKeyList, rowValues))
    {
        bulkInsert(table, rowValues);

        for (const auto& row : rowValues)
        {
            nlohmann::json object;

            for (const auto& value : row)
            {
                getFieldValueFromTuple(value, object);
            }

            if (callback)
            {
                lock.unlock();
                callback(ReturnTypeCallback::INSERTED, object);
                lock.lock();
            }
        }
    }

    return ret;
}

void SQLiteDBEngine::bulkInsert(const std::string& table, const std::vector<Row>& data)
{
    const auto stmt {getStatement(buildInsertDataSqlQuery(table))};

    for (const auto& row : data)
    {
        const auto tableFields {m_tableFields[table]};

        for (const auto& value : tableFields)
        {
            auto it {row.find(std::get<TableHeader::Name>(value))};

            if (row.end() != it)
            {
                bindFieldData(stmt, std::get<TableHeader::CID>(value) + 1, (*it).second);
            }
        }

        updateTableRowCounter(table, 1ll);

        if (SQLITE_ERROR == stmt->step())
        {

            updateTableRowCounter(table, -1ll);
            throw dbengine_error {BIND_FIELDS_DOES_NOT_MATCH};
        }

        stmt->reset();
    }
}

int SQLiteDBEngine::changeModifiedRows(const std::string& table,
                                       const std::vector<std::string>& primaryKeyList,
                                       const DbSync::ResultCallback& callback,
                                       std::unique_lock<std::shared_timed_mutex>& lock)
{
    auto ret {true};
    std::vector<Row> rowKeysValue;

    if (getRowsToModify(table, primaryKeyList, rowKeysValue))
    {
        if (updateRows(table, primaryKeyList, rowKeysValue))
        {
            for (const auto& row : rowKeysValue)
            {
                nlohmann::json object;

                for (const auto& value : row)
                {
                    getFieldValueFromTuple(value, object);
                }

                if (callback)
                {
                    lock.unlock();
                    callback(ReturnTypeCallback::MODIFIED, object);
                    lock.lock();
                }
            }
        }
        else
        {
            ret = false;
        }
    }

    return ret;
}

std::string SQLiteDBEngine::buildUpdatePartialDataSqlQuery(const std::string& table,
                                                           const nlohmann::json& data,
                                                           const std::vector<std::string>& primaryKeyList)

{
    std::string sql {"UPDATE " + table + " SET "};

    if (!primaryKeyList.empty())
    {
        for (auto it = data.begin(); it != data.end(); ++it)
        {
            if (std::find(primaryKeyList.begin(), primaryKeyList.end(), it.key()) == primaryKeyList.end())
            {
                sql += it.key() + "=?,";
            }
        }

        sql = sql.substr(0, sql.size() - 1); // Remove the last " , "
        sql.append(" WHERE ");

        for (auto it = data.begin(); it != data.end(); ++it)
        {
            if (std::find(primaryKeyList.begin(), primaryKeyList.end(), it.key()) != primaryKeyList.end())
            {
                sql += it.key() + "=? AND ";
            }
        }

        sql = sql.substr(0, sql.size() - COND_AND_SIZE); // Remove the last " AND "
        sql.append(";");
    }
    else
    {
        throw dbengine_error {SQL_STMT_ERROR};
    }

    return sql;
}

std::string SQLiteDBEngine::buildSelectMatchingPKsSqlQuery(const std::string& table,
                                                           const std::vector<std::string>& primaryKeyList)

{
    std::string sql {"SELECT * FROM "};
    sql.append(table);
    sql.append(" WHERE ");

    if (!primaryKeyList.empty())
    {
        for (const auto& value : primaryKeyList)
        {
            sql.append(value);
            sql.append("=? AND ");
        }

        sql = sql.substr(0, sql.size() - COND_AND_SIZE); // Remove the last " AND "
        sql.append(";");
    }
    else
    {
        throw dbengine_error {SQL_STMT_ERROR};
    }

    return sql;
}

std::string SQLiteDBEngine::buildUpdateDataSqlQuery(const std::string& table,
                                                    const std::vector<std::string>& primaryKeyList,
                                                    const Row& row,
                                                    const std::pair<const std::string, TableField>& field)
{
    std::string sql {"UPDATE "};
    sql.append(table);
    sql.append(" SET ");
    sql.append(field.first);
    sql.append("=");
    getFieldValueFromTuple(field, sql, true);
    sql.append(" WHERE ");

    if (!primaryKeyList.empty())
    {
        for (const auto& value : primaryKeyList)
        {
            const auto it {row.find("PK_" + value)};

            if (it != row.end())
            {
                sql.append(value);
                sql.append("=");
                getFieldValueFromTuple((*it), sql, true);
            }
            else
            {
                sql.clear();
                break;
            }

            sql.append(" AND ");
        }

        sql = sql.substr(0, sql.length() - COND_AND_SIZE); // Remove the last " AND "

        if (!sql.empty())
        {
            sql.append(";");
        }
    }
    else
    {
        throw dbengine_error {SQL_STMT_ERROR};
    }

    return sql;
}

std::string SQLiteDBEngine::buildModifiedRowsQuery(const std::string& t1,
                                                   const std::string& t2,
                                                   const std::vector<std::string>& primaryKeyList)
{
    std::string fieldsList;
    std::string onMatchList;

    for (const auto& value : primaryKeyList)
    {
        fieldsList.append("t1." + value + ",");
        onMatchList.append("t1." + value);
        onMatchList.append("=t2." + value + " AND ");
    }

    const auto tableFields {m_tableFields[t1]};

    for (const auto& value : tableFields)
    {
        const auto fieldName {std::get<TableHeader::Name>(value)};
        fieldsList.append("CASE WHEN t1.");
        fieldsList.append(fieldName);
        fieldsList.append("<>t2.");
        fieldsList.append(fieldName);
        fieldsList.append(" THEN t1.");
        fieldsList.append(fieldName);
        fieldsList.append(" ELSE NULL END AS DIF_");
        fieldsList.append(fieldName);
        fieldsList.append(",");
    }

    fieldsList = fieldsList.substr(0, fieldsList.size() - 1);
    onMatchList = onMatchList.substr(0, onMatchList.size() - COND_AND_SIZE); // Remove the last " AND "
    std::string ret {"SELECT "};
    ret.append(fieldsList);
    ret.append(" FROM (select *,'");
    ret.append(t1);
    ret.append("' as val from ");
    ret.append(t1);
    ret.append(" UNION ALL select *,'");
    ret.append(t2);
    ret.append("' as val from ");
    ret.append(t2);
    ret.append(") t1 INNER JOIN ");
    ret.append(t1);
    ret.append(" t2 ON ");
    ret.append(onMatchList);
    ret.append(" WHERE t1.val = '");
    ret.append(t2);
    ret.append("';");

    return ret;
}

bool SQLiteDBEngine::getRowsToModify(const std::string& table,
                                     const std::vector<std::string>& primaryKeyList,
                                     std::vector<Row>& rowKeysValue)
{
    auto ret {false};
    auto sql {buildModifiedRowsQuery(table, table + TEMP_TABLE_SUBFIX, primaryKeyList)};

    if (!sql.empty())
    {
        const auto stmt {getStatement(sql)};

        while (SQLITE_ROW == stmt->step())
        {
            bool dataModified {false};
            const auto tableFields {m_tableFields[table]};
            Row registerFields;
            int32_t index {0l};

            for (const auto& pkValue : primaryKeyList)
            {
                const auto it {std::find_if(tableFields.begin(),
                                            tableFields.end(),
                                            [&pkValue](const ColumnData& cd)
                                            { return std::get<TableHeader::Name>(cd).compare(pkValue) == 0; })};

                if (tableFields.end() != it)
                {
                    getTableData(stmt,
                                 index,
                                 std::get<TableHeader::Type>(*it),
                                 "PK_" + std::get<TableHeader::Name>(*it),
                                 registerFields);
                }

                ++index;
            }

            for (const auto& field : tableFields)
            {
                if (registerFields.end() == registerFields.find(std::get<TableHeader::Name>(field)))
                {
                    if (stmt->column(index)->hasValue())
                    {
                        dataModified = true;
                        getTableData(stmt,
                                     index,
                                     std::get<TableHeader::Type>(field),
                                     std::get<TableHeader::Name>(field),
                                     registerFields);
                    }
                }

                ++index;
            }

            if (dataModified)
            {
                rowKeysValue.push_back(std::move(registerFields));
            }
        }

        ret = true;
    }
    else
    {
        throw dbengine_error {SQL_STMT_ERROR};
    }

    return ret;
}

void SQLiteDBEngine::updateSingleRow(const std::string& table, const nlohmann::json& jsData)
{
    std::vector<std::string> primaryKeyList;

    if (getPrimaryKeysFromTable(table, primaryKeyList))
    {
        const auto& tableFields {m_tableFields[table]};
        const auto stmt {getStatement(buildUpdatePartialDataSqlQuery(table, jsData, primaryKeyList))};
        int32_t index {1l};

        for (auto it = jsData.begin(); it != jsData.end(); ++it)
        {
            if (std::find(primaryKeyList.begin(), primaryKeyList.end(), it.key()) == primaryKeyList.end())
            {
                const auto it1 {std::find_if(tableFields.begin(),
                                             tableFields.end(),
                                             [&it](const auto& value)
                                             { return std::get<GenericTupleIndex::GenString>(value) == it.key(); })};

                if (it1 == tableFields.end())
                {
                    throw dbengine_error {BIND_FIELDS_DOES_NOT_MATCH};
                }

                bindJsonData(stmt, *it1, jsData, index);
                ++index;
            }
        }

        for (auto it = jsData.begin(); it != jsData.end(); ++it)
        {
            if (std::find(primaryKeyList.begin(), primaryKeyList.end(), it.key()) != primaryKeyList.end())
            {
                const auto it1 {std::find_if(tableFields.begin(),
                                             tableFields.end(),
                                             [&it](const auto& value)
                                             { return std::get<GenericTupleIndex::GenString>(value) == it.key(); })};

                if (it1 == tableFields.end())
                {
                    throw dbengine_error {BIND_FIELDS_DOES_NOT_MATCH};
                }

                bindJsonData(stmt, *it1, jsData, index);
                ++index;
            }
        }

        if (SQLITE_ERROR == stmt->step())
        {
            throw dbengine_error {BIND_FIELDS_DOES_NOT_MATCH};
        }

        stmt->reset();
    }
}

bool SQLiteDBEngine::updateRows(const std::string& table,
                                const std::vector<std::string>& primaryKeyList,
                                const std::vector<Row>& rowKeysValue)
{

    for (const auto& row : rowKeysValue)
    {
        for (const auto& field : row)
        {
            if (0 != field.first.substr(0, 3).compare("PK_"))
            {
                const auto sql {buildUpdateDataSqlQuery(table, primaryKeyList, row, field)};
                m_sqliteConnection->execute(sql);
            }
        }
    }

    return true;
}

void SQLiteDBEngine::getFieldValueFromTuple(const Field& value, nlohmann::json& object)
{
    const auto rowType {std::get<GenericTupleIndex::GenType>(value.second)};

    if (ColumnType::BigInt == rowType)
    {
        const auto& optValue = std::get<ColumnType::BigInt>(value.second);
        object[value.first] = optValue.has_value() ? nlohmann::json(optValue.value()) : nlohmann::json(nullptr);
    }
    else if (ColumnType::UnsignedBigInt == rowType)
    {
        const auto& optValue = std::get<ColumnType::UnsignedBigInt>(value.second);
        object[value.first] = optValue.has_value() ? nlohmann::json(optValue.value()) : nlohmann::json(nullptr);
    }
    else if (ColumnType::Integer == rowType)
    {
        const auto& optValue = std::get<ColumnType::Integer>(value.second);
        object[value.first] = optValue.has_value() ? nlohmann::json(optValue.value()) : nlohmann::json(nullptr);
    }
    else if (ColumnType::Text == rowType)
    {
        const auto& optValue = std::get<ColumnType::Text>(value.second);
        object[value.first] = optValue.has_value() ? nlohmann::json(optValue.value()) : nlohmann::json(nullptr);
    }
    else if (ColumnType::Double == rowType)
    {
        const auto& optValue = std::get<ColumnType::Double>(value.second);
        object[value.first] = optValue.has_value() ? nlohmann::json(optValue.value()) : nlohmann::json(nullptr);
    }
    else
    {
        throw dbengine_error {DATATYPE_NOT_IMPLEMENTED};
    }
}

void SQLiteDBEngine::getFieldValueFromTuple(const Field& value, std::string& resultValue, const bool quotationMarks)
{
    const auto rowType {std::get<GenericTupleIndex::GenType>(value.second)};

    if (ColumnType::BigInt == rowType)
    {
        const auto& optValue = std::get<ColumnType::BigInt>(value.second);
        resultValue.append(optValue.has_value() ? std::to_string(optValue.value()) : "null");
    }
    else if (ColumnType::UnsignedBigInt == rowType)
    {
        const auto& optValue = std::get<ColumnType::UnsignedBigInt>(value.second);
        resultValue.append(optValue.has_value() ? std::to_string(optValue.value()) : "null");
    }
    else if (ColumnType::Integer == rowType)
    {
        const auto& optValue = std::get<ColumnType::Integer>(value.second);
        resultValue.append(optValue.has_value() ? std::to_string(optValue.value()) : "null");
    }
    else if (ColumnType::Text == rowType)
    {
        const auto& optValue = std::get<ColumnType::Text>(value.second);
        if (optValue.has_value())
        {
            if (quotationMarks)
            {
                resultValue.append("'" + optValue.value() + "'");
            }
            else
            {
                resultValue.append(optValue.value());
            }
        }
        else
        {
            resultValue.append("null");
        }
    }
    else if (ColumnType::Double == rowType)
    {
        const auto& optValue = std::get<ColumnType::Double>(value.second);
        resultValue.append(optValue.has_value() ? std::to_string(optValue.value()) : "null");
    }
    else
    {
        throw dbengine_error {DATATYPE_NOT_IMPLEMENTED};
    }
}

std::shared_ptr<SQLiteLegacy::IStatement> SQLiteDBEngine::getStatement(const std::string& sql)
{
    const std::lock_guard<std::mutex> lock(m_stmtMutex);
    const auto it {std::find_if(m_statementsCache.begin(),
                                m_statementsCache.end(),
                                [sql](const std::pair<std::string, std::shared_ptr<SQLiteLegacy::IStatement>>& pair)
                                { return 0 == pair.first.compare(sql); })};

    if (m_statementsCache.end() != it)
    {
        it->second->reset();
        return it->second;
    }
    else
    {
        m_statementsCache.emplace_back(sql, m_sqliteFactory->createStatement(m_sqliteConnection, sql));

        if (CACHE_STMT_LIMIT <= m_statementsCache.size())
        {
            m_statementsCache.pop_front();
        }

        return m_statementsCache.back().second;
    }
}

std::string SQLiteDBEngine::getSelectAllQuery(const std::string& table, const TableColumns& tableFields) const
{
    std::string retVal {"SELECT "};

    if (!tableFields.empty() && !table.empty())
    {
        for (const auto& field : tableFields)
        {
            if (!std::get<TableHeader::TXNStatusField>(field))
            {
                retVal.append(std::get<TableHeader::Name>(field));
                retVal.append(",");
            }
        }

        retVal = retVal.substr(0, retVal.size() - 1);
        retVal.append(" FROM ");
        retVal.append(table);
        retVal.append(" WHERE ");
        retVal.append(STATUS_FIELD_NAME);
        retVal.append("=0;");
    }
    else
    {
        throw dbengine_error {EMPTY_TABLE_METADATA};
    }

    return retVal;
}

std::string SQLiteDBEngine::buildDeleteRelationTrigger(const nlohmann::json& data, const std::string& baseTable)
{
    const constexpr auto DELETE_POSTFIX {"_delete"};

    auto sqlDelete {"CREATE TRIGGER IF NOT EXISTS " + baseTable + DELETE_POSTFIX + " BEFORE DELETE ON " + baseTable};

    sqlDelete.append(" BEGIN ");

    for (const auto& jsonValue : data.at("relationed_tables"))
    {
        sqlDelete.append("DELETE FROM " + jsonValue.at("table").get<std::string>() + " WHERE ");

        for (const auto& match : jsonValue.at("field_match").items())
        {
            sqlDelete.append(match.key());
            sqlDelete.append(" = OLD.");
            sqlDelete.append(match.value().get_ref<const std::string&>());
            sqlDelete.append(" AND ");
        }

        sqlDelete = sqlDelete.substr(0, sqlDelete.size() - COND_AND_SIZE); // Remove the last " AND "
        sqlDelete.append(";");
    }

    sqlDelete.append("END;");
    return sqlDelete;
}

std::string SQLiteDBEngine::buildUpdateRelationTrigger(const nlohmann::json& data,
                                                       const std::string& baseTable,
                                                       const std::vector<std::string>& primaryKeys)
{
    const constexpr auto UPDATE_POSTFIX {"_update"};

    auto sqlUpdate {"CREATE TRIGGER IF NOT EXISTS " + baseTable + UPDATE_POSTFIX + " BEFORE UPDATE OF "};

    for (const auto& pkName : primaryKeys)
    {
        sqlUpdate.append(pkName);
        sqlUpdate.append(",");
    }

    sqlUpdate = sqlUpdate.substr(0, sqlUpdate.size() - 1);

    sqlUpdate.append(" ON " + baseTable);

    sqlUpdate.append(" BEGIN ");

    for (const auto& jsonValue : data.at("relationed_tables"))
    {
        sqlUpdate.append("UPDATE " + jsonValue.at("table").get<std::string>() + " SET ");

        auto sqlUpdateWhere {std::string(" WHERE ")};

        for (const auto& match : jsonValue.at("field_match").items())
        {
            sqlUpdate.append(match.key());
            sqlUpdate.append(" = NEW.");
            sqlUpdate.append(match.value().get_ref<const std::string&>());
            sqlUpdate.append(",");

            sqlUpdateWhere.append(match.key());
            sqlUpdateWhere.append(" = OLD.");
            sqlUpdateWhere.append(match.value().get_ref<const std::string&>());
            sqlUpdateWhere.append(" AND ");
        }

        sqlUpdate = sqlUpdate.substr(0, sqlUpdate.size() - 1);
        sqlUpdateWhere = sqlUpdateWhere.substr(0, sqlUpdateWhere.size() - COND_AND_SIZE); // Remove the last " AND "
        sqlUpdate.append(sqlUpdateWhere);
        sqlUpdate.append(";");
    }

    sqlUpdate.append("END;");
    return sqlUpdate;
}

void SQLiteDBEngine::updateTableRowCounter(const std::string& table, const long long rowModifyCount)
{
    const std::lock_guard<std::mutex> lock(m_maxRowsMutex);
    auto it {m_maxRows.find(table)};

    if (it != m_maxRows.end())
    {
        if (it->second.currentRows + rowModifyCount > it->second.maxRows)
        {
            throw DbSync::max_rows_error {SQLiteLegacy::MAX_ROWS_ERROR_STRING};
        }

        it->second.currentRows += rowModifyCount;

        if (it->second.currentRows < 0)
        {
            it->second.currentRows = 0;
            throw dbengine_error {ERROR_COUNT_MAX_ROWS};
        }
    }
}
