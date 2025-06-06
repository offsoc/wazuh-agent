#include <sca_event_handler.hpp>

#include <dbsync.hpp>
#include <hashHelper.hpp>
#include <logger.hpp>
#include <stringHelper.hpp>
#include <timeHelper.hpp>

#include <sstream>

/// @brief Map of stateful operations
static const std::map<ReturnTypeCallback, std::string> STATEFUL_OPERATION_MAP {
    {MODIFIED, "update"},
    {DELETED, "delete"},
    {INSERTED, "create"},
    {MAX_ROWS, "max_rows"},
    {DB_ERROR, "db_error"},
    {SELECTED, "selected"},
};

/// @brief Map of stateless operations
static const std::map<ReturnTypeCallback, std::string> STATELESS_OPERATION_MAP {
    {MODIFIED, "change"},
    {DELETED, "deletion"},
    {INSERTED, "creation"},
};

SCAEventHandler::SCAEventHandler(std::string agentUUID,
                                 std::shared_ptr<IDBSync> dBSync,
                                 std::function<int(Message)> pushMessage)
    : m_agentUUID(std::move(agentUUID))
    , m_dBSync(std::move(dBSync))
    , m_pushMessage(std::move(pushMessage)) {};

void SCAEventHandler::ReportPoliciesDelta(
    const std::unordered_map<std::string, nlohmann::json>& modifiedPoliciesMap,
    const std::unordered_map<std::string, nlohmann::json>& modifiedChecksMap) const
{
    const nlohmann::json events = ProcessEvents(modifiedPoliciesMap, modifiedChecksMap);

    for (const auto& event : events)
    {
        const nlohmann::json processedStatefulEvent = ProcessStateful(event);
        if (!processedStatefulEvent.empty())
        {
            PushStateful(processedStatefulEvent["event"], processedStatefulEvent["metadata"]);
        }
        const nlohmann::json processedStatelessEvent = ProcessStateless(event);
        if (!processedStatelessEvent.empty())
        {
            PushStateless(processedStatelessEvent["event"], processedStatelessEvent["metadata"]);
        }
    }
}

void SCAEventHandler::ReportCheckResult(const std::string& policyId,
                                        const std::string& checkId,
                                        const std::string& checkResult) const
{
    auto policyData = GetPolicyById(policyId);
    auto checkData = GetPolicyCheckById(checkId);
    checkData["result"] = checkResult;

    auto updateResultQuery = SyncRowQuery::builder().table("sca_check").data(checkData).returnOldData().build();

    const auto callback = [&, this](ReturnTypeCallback result, const nlohmann::json& rowData)
    {
        if (result == MODIFIED)
        {
            const nlohmann::json event = {
                {"policy", policyData}, {"check", rowData}, {"result", result}, {"collector", "check"}};

            const auto stateful = ProcessStateful(event);
            PushStateful(stateful["event"], stateful["metadata"]);
            const auto stateless = ProcessStateless(event);
            PushStateless(stateless["event"], stateless["metadata"]);
        }
        else
        {
            LogDebug("Failed to update check result: {}", rowData.dump());
        }
    };

    m_dBSync->syncRow(updateResultQuery.query(), callback);
}

nlohmann::json
SCAEventHandler::ProcessEvents(const std::unordered_map<std::string, nlohmann::json>& modifiedPoliciesMap,
                               const std::unordered_map<std::string, nlohmann::json>& modifiedChecksMap) const
{
    nlohmann::json events = nlohmann::json::array();
    try
    {
        for (const auto& policyEntry : modifiedPoliciesMap)
        {
            const std::string policyId = policyEntry.first;
            const nlohmann::json policyData = policyEntry.second["data"];
            const int policyResult = policyEntry.second["result"];

            const std::vector<nlohmann::json> checksForPolicy = GetChecksForPolicy(policyId);

            for (auto checkData : checksForPolicy)
            {
                const std::string checkId = checkData["id"];

                if (modifiedChecksMap.find(checkId) != modifiedChecksMap.end())
                {
                    continue;
                }

                const nlohmann::json event = {
                    {"policy", policyData}, {"check", checkData}, {"result", policyResult}, {"collector", "policy"}};
                events.push_back(event);
            }
        }

        for (const auto& checkEntry : modifiedChecksMap)
        {
            nlohmann::json policyData;
            nlohmann::json checkData = checkEntry.second["data"];
            int checkResult = checkEntry.second["result"];

            std::string policyId;
            if (checkResult == MODIFIED)
            {
                policyId = checkData["new"]["policy_id"];
            }
            else
            {
                policyId = checkData["policy_id"];
            }

            if (modifiedPoliciesMap.find(policyId) != modifiedPoliciesMap.end())
            {
                policyData = modifiedPoliciesMap.at(policyId)["data"];
            }
            else
            {
                policyData = GetPolicyById(policyId);
            }

            const nlohmann::json event = {
                {"policy", policyData}, {"check", checkData}, {"result", checkResult}, {"collector", "check"}};
            events.push_back(event);
        }
    }
    catch (const std::exception& e)
    {
        LogError("Failed to create events: {}", e.what());
    }

    return events;
}

std::vector<nlohmann::json> SCAEventHandler::GetChecksForPolicy(const std::string& policyId) const
{
    std::vector<nlohmann::json> checks;

    const std::string filter = "WHERE policy_id = '" + policyId + "'";
    auto selectQuery = SelectQuery::builder()
                           .table("sca_check")
                           .columnList({"id",
                                        "policy_id",
                                        "name",
                                        "description",
                                        "rationale",
                                        "remediation",
                                        "refs",
                                        "result",
                                        "reason",
                                        "condition",
                                        "compliance",
                                        "rules"})
                           .rowFilter(filter)
                           .build();

    const auto callback = [&checks](ReturnTypeCallback returnTypeCallback, const nlohmann::json& resultData)
    {
        if (returnTypeCallback == SELECTED)
        {
            checks.push_back(resultData);
        }
    };

    m_dBSync->selectRows(selectQuery.query(), callback);

    return checks;
}

nlohmann::json SCAEventHandler::GetPolicyById(const std::string& policyId) const
{
    nlohmann::json policy;

    const std::string filter = "WHERE id = '" + policyId + "'";
    auto selectQuery = SelectQuery::builder()
                           .table("sca_policy")
                           .columnList({"id", "name", "description", "file", "refs"})
                           .rowFilter(filter)
                           .build();

    const auto callback = [&policy](ReturnTypeCallback returnTypeCallback, const nlohmann::json& resultData)
    {
        if (returnTypeCallback == SELECTED)
        {
            policy = resultData;
        }
    };

    m_dBSync->selectRows(selectQuery.query(), callback);

    return policy;
}

nlohmann::json SCAEventHandler::GetPolicyCheckById(const std::string& policyCheckId) const
{
    nlohmann::json check;

    const std::string filter = "WHERE id = '" + policyCheckId + "'";
    auto selectQuery = SelectQuery::builder()
                           .table("sca_check")
                           .columnList({"id",
                                        "policy_id",
                                        "name",
                                        "description",
                                        "rationale",
                                        "remediation",
                                        "refs",
                                        "result",
                                        "reason",
                                        "condition",
                                        "compliance",
                                        "rules"})
                           .rowFilter(filter)
                           .build();

    const auto callback = [&check](ReturnTypeCallback returnTypeCallback, const nlohmann::json& resultData)
    {
        if (returnTypeCallback == SELECTED)
        {
            check = resultData;
        }
    };

    m_dBSync->selectRows(selectQuery.query(), callback);

    return check;
}

nlohmann::json SCAEventHandler::ProcessStateful(const nlohmann::json& event) const
{
    nlohmann::json check;
    nlohmann::json policy;
    nlohmann::json jsonEvent;
    nlohmann::json jsonMetadata;

    try
    {
        if (event.contains("check") && event["check"].is_object())
        {
            if (event["check"].contains("new") && event["check"]["new"].is_object())
            {
                check = event["check"]["new"];
            }
            else
            {
                check = event["check"];
            }
        }
        else
        {
            LogError("Stateful event does not contain check");
            return {};
        }

        if (event.contains("policy") && event["policy"].is_object())
        {
            if (event["policy"].contains("new") && event["policy"]["new"].is_object())
            {
                policy = event["policy"]["new"];
            }
            else
            {
                policy = event["policy"];
            }
        }
        else
        {
            LogError("Stateful event does not contain policy");
            return {};
        }

        NormalizeCheck(check);
        NormalizePolicy(policy);

        jsonEvent = {{"policy", policy}, {"check", check}, {"timestamp", Utils::getCurrentISO8601()}};
        jsonMetadata = {{"id", CalculateHashId(jsonEvent)},
                        {"operation", STATEFUL_OPERATION_MAP.at(event["result"])},
                        {"module", "sca"}};
    }
    catch (const std::exception& e)
    {
        LogError("Error processing stateful event: {}", e.what());
    }

    return nlohmann::json {{"event", jsonEvent}, {"metadata", jsonMetadata}};
}

nlohmann::json SCAEventHandler::ProcessStateless(const nlohmann::json& event) const
{
    nlohmann::json check;
    nlohmann::json policy;
    nlohmann::json changedFields = nlohmann::json::array();
    nlohmann::json jsonEvent;
    nlohmann::json jsonMetadata;

    try
    {
        if (event.contains("check") && event["check"].is_object())
        {
            if (event["check"].contains("new") && event["check"]["new"].is_object())
            {
                check = event["check"]["new"];
            }
            else
            {
                check = event["check"];
            }

            if (event["check"].contains("old") && event["check"]["old"].is_object())
            {
                const auto& old = event["check"]["old"];
                nlohmann::json previous;

                for (auto& [key, value] : old.items())
                {
                    if (key == "id")
                    {
                        continue;
                    }
                    previous[key] = value;
                    changedFields.push_back("check." + key);
                }
                check["previous"] = previous;
            }
        }
        else
        {
            LogError("Stateless event does not contain check");
            return {};
        }

        if (event.contains("policy") && event["policy"].is_object())
        {
            if (event["policy"].contains("new") && event["policy"]["new"].is_object())
            {
                policy = event["policy"]["new"];
            }
            else
            {
                policy = event["policy"];
            }

            if (event["policy"].contains("old") && event["policy"]["old"].is_object())
            {
                const auto& old = event["policy"]["old"];
                nlohmann::json previous;

                for (auto& [key, value] : old.items())
                {
                    if (key == "id")
                    {
                        continue;
                    }
                    previous[key] = value;
                    changedFields.push_back("policy." + key);
                }
                policy["previous"] = previous;
            }
        }
        else
        {
            LogError("Stateless event does not contain policy");
            return {};
        }

        NormalizeCheck(check);
        NormalizePolicy(policy);

        jsonEvent = {
            {"event",
             {
                 {"created", Utils::getCurrentISO8601()},
                 {"category", {"configuration"}},
                 {"type", STATELESS_OPERATION_MAP.at(event["result"])},
                 {"action",
                  {event.at("collector").get<std::string>() + "-" + STATELESS_OPERATION_MAP.at(event["result"])}},
                 {"changed_fields", changedFields},
             }},
            {"policy", policy},
            {"check", check}};

        jsonMetadata = {{"module", "sca"}, {"collector", event.at("collector")}};
    }
    catch (const std::exception& e)
    {
        LogError("Error processing stateless event: {}", e.what());
    }

    return nlohmann::json {{"event", jsonEvent}, {"metadata", jsonMetadata}};
}

std::string SCAEventHandler::CalculateHashId(const nlohmann::json& data) const
{
    const std::string baseId =
        m_agentUUID + ":" + data["policy"]["id"].get<std::string>() + ":" + data["check"]["id"].get<std::string>();

    Utils::HashData hash(Utils::HashType::Sha1);
    hash.update(baseId.c_str(), baseId.size());

    return Utils::asciiToHex(hash.hash());
}

void SCAEventHandler::PushStateful(const nlohmann::json& event, const nlohmann::json& metadata) const
{
    if (!m_pushMessage)
    {
        throw std::runtime_error("Message queue not set, cannot send message.");
    }

    const Message statefulMessage {MessageType::STATEFUL,
                                   metadata["operation"] == "delete" ? "{}"_json : event,
                                   metadata["module"],
                                   "",
                                   metadata.dump()};

    m_pushMessage(statefulMessage);

    LogTrace("Stateful event queued: {}, metadata {}", event.dump(), metadata.dump());
}

void SCAEventHandler::PushStateless(const nlohmann::json& event, const nlohmann::json& metadata) const
{
    if (!m_pushMessage)
    {
        throw std::runtime_error("Message queue not set, cannot send message.");
    }

    const Message statelessMessage {
        MessageType::STATELESS, event, metadata["module"], metadata["collector"], metadata.dump()};

    m_pushMessage(statelessMessage);

    LogTrace("Stateless event queued: {}, metadata {}", event.dump(), metadata.dump());
}

nlohmann::json SCAEventHandler::StringToJsonArray(const std::string& input) const
{
    nlohmann::json result = nlohmann::json::array();
    std::istringstream stream(input);
    std::string token;

    while (std::getline(stream, token, ','))
    {
        token = Utils::Trim(token, " \t");

        if (!token.empty())
        {
            result.push_back(token);
        }
    }

    return result;
}

void SCAEventHandler::NormalizeCheck(nlohmann::json& check) const
{
    if (check.contains("refs") && check["refs"].is_string())
    {
        check["references"] = StringToJsonArray(check["refs"].get<std::string>());
        check.erase("refs");
    }

    if (check.contains("compliance") && check["compliance"].is_string())
    {
        check["compliance"] = StringToJsonArray(check["compliance"].get<std::string>());
    }

    if (check.contains("rules") && check["rules"].is_string())
    {
        check["rules"] = StringToJsonArray(check["rules"].get<std::string>());
    }

    if (check.contains("policy_id"))
    {
        check.erase("policy_id");
    }
}

void SCAEventHandler::NormalizePolicy(nlohmann::json& policy) const
{
    if (policy.contains("refs") && policy["refs"].is_string())
    {
        policy["references"] = StringToJsonArray(policy["refs"].get<std::string>());
        policy.erase("refs");
    }
}
