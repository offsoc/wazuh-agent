#include "osinfo/sysOsParsers.h"
#include "sharedDefs.h"
#include "stringHelper.hpp"
#include <map>
#include <sstream>
#include <string>
#include <vector>

static bool ParseUnixFile(const std::vector<std::pair<std::string, std::string>>& keyMapping,
                          const char separator,
                          std::istream& in,
                          nlohmann::json& info)
{
    bool ret {false};
    const std::string fileContent(std::istreambuf_iterator<char>(in), {});
    std::map<std::string, std::string> fileKeyValueMap;
    std::map<std::string, std::string>::iterator itFileKeyValue;

    Utils::splitMapKeyValue(fileContent, separator, fileKeyValueMap);

    for (const auto& keyMappingEntry : keyMapping)
    {
        if (info.find(keyMappingEntry.second) == info.end())
        {
            itFileKeyValue = fileKeyValueMap.find(keyMappingEntry.first);
            if (itFileKeyValue != fileKeyValueMap.end())
            {
                info[keyMappingEntry.second] = itFileKeyValue->second;
                ret = true;
            }
        }
    }

    return ret;
}

static bool FindCodeNameInString(const std::string& in, std::string& output)
{
    const auto end {in.rfind(')')};
    const auto start {in.rfind('(')};
    const bool ret {start != std::string::npos && end != std::string::npos};

    if (ret)
    {
        output = Utils::Trim(in.substr(start + 1, end - (start + 1)));
    }

    return ret;
}

static void FindMajorMinorVersionInString(const std::string& in, nlohmann::json& output)
{
    constexpr auto PATCH_VERSION_PATTERN {R"(^[0-9]+\.[0-9]+\.([0-9]+)\.*)"};
    constexpr auto MINOR_VERSION_PATTERN {R"(^[0-9]+\.([0-9]+)\.*)"};
    constexpr auto MAJOR_VERSION_PATTERN {R"(^([0-9]+)\.*)"};
    std::string version;
    std::regex pattern {MAJOR_VERSION_PATTERN};

    if (Utils::FindRegexInString(in, version, pattern, 1))
    {
        output["os_major"] = version;
    }

    pattern = MINOR_VERSION_PATTERN;

    if (Utils::FindRegexInString(in, version, pattern, 1))
    {
        output["os_minor"] = version;
    }

    pattern = PATCH_VERSION_PATTERN;

    if (Utils::FindRegexInString(in, version, pattern, 1))
    {
        output["os_patch"] = version;
    }
}

static bool FindVersionInStream(std::istream& in,
                                nlohmann::json& output,
                                const std::string& regex,
                                const size_t matchIndex = 0,
                                const std::string& start = "")
{
    bool ret {false};
    std::string line;
    std::string data;
    const std::regex pattern {regex};

    while (std::getline(in, line))
    {
        line = Utils::Trim(line);
        ret |= Utils::FindRegexInString(line, data, pattern, matchIndex, start);

        if (ret)
        {
            output["os_version"] = data;
            FindMajorMinorVersionInString(data, output);
        }

        if (FindCodeNameInString(line, data))
        {
            output["os_codename"] = data;
        }
    }

    return ret;
}

bool UnixOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    constexpr auto SEPARATOR {'='};
    static const std::vector<std::pair<std::string, std::string>> KEY_MAPPING {{"NAME", "os_name"},
                                                                               {"VERSION", "os_version"},
                                                                               {"VERSION_ID", "os_version"},
                                                                               {"ID", "os_platform"},
                                                                               {"BUILD_ID", "os_build"},
                                                                               {"VERSION_CODENAME", "os_codename"}};
    const auto ret {ParseUnixFile(KEY_MAPPING, SEPARATOR, in, output)};

    if (ret && output.find("os_version") != output.end())
    {
        FindMajorMinorVersionInString(output["os_version"], output);
    }

    return ret;
}

bool UbuntuOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    constexpr auto PATTERN_MATCH {R"([0-9].*\.[0-9]*)"};
    constexpr auto DISTRIB_FIELD {"DISTRIB_DESCRIPTION"};
    static const std::string CODENAME_FIELD {"DISTRIB_CODENAME"};
    bool ret {false};
    std::string line;
    const std::regex pattern {PATTERN_MATCH};

    while (std::getline(in, line))
    {
        line = Utils::Trim(line);
        std::string match;

        if (Utils::FindRegexInString(line, match, pattern, 0, DISTRIB_FIELD))
        {
            output["os_version"] = match;
            FindMajorMinorVersionInString(match, output);
            ret = true;
        }
        else if (Utils::startsWith(line, CODENAME_FIELD))
        {
            output["os_codename"] = Utils::Trim(line.substr(CODENAME_FIELD.size()), " =");
            ret = true;
        }
    }

    output["os_name"] = "Ubuntu";
    output["os_platform"] = "ubuntu";
    return ret;
}

bool CentosOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    constexpr auto PATTERN_MATCH {R"([0-9].*\.[0-9]*)"};

    if (!output.contains("os_name"))
    {
        output["os_name"] = "Centos Linux";
    }

    if (!output.contains("os_platform"))
    {
        output["os_platform"] = "centos";
    }

    return FindVersionInStream(in, output, PATTERN_MATCH);
}

bool BSDOsParser::parseUname(const std::string& in, nlohmann::json& output)
{
    constexpr auto PATTERN_MATCH {R"([0-9].*\.[0-9]*)"};
    std::string match;
    const std::regex pattern {PATTERN_MATCH};
    const auto ret {Utils::FindRegexInString(in, match, pattern)};

    if (ret)
    {
        output["os_version"] = match;
        FindMajorMinorVersionInString(match, output);
    }

    output["os_name"] = "BSD";
    output["os_platform"] = "bsd";
    return ret;
}

bool RedHatOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    static const std::string FIRST_DELIMITER {"release"};
    static const std::string SECOND_DELIMITER {"("};
    bool ret {false};
    std::string data;

    if (std::getline(in, data))
    {
        // format is: OSNAME release VERSION (CODENAME)
        auto pos {data.find(FIRST_DELIMITER)};

        if (pos != std::string::npos)
        {
            output["os_name"] = Utils::Trim(data.substr(0, pos));
            data = data.substr(pos + FIRST_DELIMITER.size());
            pos = data.find(SECOND_DELIMITER);
            ret = true;
        }

        if (pos != std::string::npos)
        {
            const auto fullVersion {Utils::Trim(data.substr(0, pos))};
            const auto versions {Utils::split(fullVersion, '.')};
            output["os_version"] = fullVersion;
            output["os_major"] = versions[0];

            if (versions.size() > 1)
            {
                output["os_minor"] = versions[1];
            }

            output["os_codename"] = Utils::Trim(data.substr(pos), " ()");
        }

        output["os_platform"] = "rhel";
    }

    return ret;
}

bool DebianOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    constexpr auto PATTERN_MATCH {R"([0-9].*\.[0-9]*)"};
    output["os_name"] = "Debian GNU/Linux";
    output["os_platform"] = "debian";
    return FindVersionInStream(in, output, PATTERN_MATCH);
}

bool SlackwareOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    constexpr auto PATTERN_MATCH {R"([0-9].*\.[0-9]*)"};
    output["os_name"] = "Slackware";
    output["os_platform"] = "slackware";
    return FindVersionInStream(in, output, PATTERN_MATCH);
}

bool GentooOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    constexpr auto PATTERN_MATCH {R"([0-9].*\.[0-9]*)"};
    output["os_name"] = "Gentoo";
    output["os_platform"] = "gentoo";
    return FindVersionInStream(in, output, PATTERN_MATCH);
}

bool SuSEOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    constexpr auto SEPARATOR {'='};
    static const std::vector<std::pair<std::string, std::string>> KEY_MAPPING {
        {"VERSION", "os_version"},
        {"CODENAME", "os_codename"},
    };
    output["os_name"] = "SuSE Linux";
    output["os_platform"] = "suse";
    const auto ret {ParseUnixFile(KEY_MAPPING, SEPARATOR, in, output)};

    if (ret)
    {
        FindMajorMinorVersionInString(output["os_version"], output);
    }

    return ret;
}

bool FedoraOsParser::parseFile(std::istream& in, nlohmann::json& output)
{
    constexpr auto PATTERN_MATCH {R"([0-9]+\.*)"};
    output["os_name"] = "Fedora";
    output["os_platform"] = "fedora";
    const auto ret {FindVersionInStream(in, output, PATTERN_MATCH)};

    if (ret)
    {
        FindMajorMinorVersionInString(output["os_version"], output);
    }

    return ret;
}

bool MacOsParser::parseSwVersion(const std::string& in, nlohmann::json& output)
{
    constexpr auto SEPARATOR {':'};
    static const std::vector<std::pair<std::string, std::string>> KEY_MAPPING {
        {"ProductVersion", "os_version"},
        {"BuildVersion", "os_build"},
    };
    output["os_platform"] = "darwin";
    std::stringstream data {in};
    const auto ret {ParseUnixFile(KEY_MAPPING, SEPARATOR, data, output)};

    if (ret)
    {
        FindMajorMinorVersionInString(output["os_version"], output);
    }

    return ret;
}

bool MacOsParser::parseSystemProfiler(const std::string& in, nlohmann::json& output)
{
    constexpr auto SEPARATOR {':'};
    static const std::vector<std::pair<std::string, std::string>> KEY_MAPPING {
        {"System Version", "os_name"},
    };
    std::stringstream data {in};
    nlohmann::json info;
    auto ret {ParseUnixFile(KEY_MAPPING, SEPARATOR, data, info)};

    if (ret)
    {
        constexpr auto PATTERN_MATCH {R"(^([^\s]+) [^\s]+ [^\s]+$)"};
        std::string match;
        const std::regex pattern {PATTERN_MATCH};

        if (Utils::FindRegexInString(info["os_name"], match, pattern, 1))
        {
            output["os_name"] = std::move(match);
        }
        else
        {
            ret = false;
        }
    }

    return ret;
}

bool MacOsParser::parseUname(const std::string& in, nlohmann::json& output)
{
    static const std::map<std::string, std::string> MAC_CODENAME_MAP {
        {"10", "Snow Leopard"},
        {"11", "Lion"},
        {"12", "Mountain Lion"},
        {"13", "Mavericks"},
        {"14", "Yosemite"},
        {"15", "El Capitan"},
        {"16", "Sierra"},
        {"17", "High Sierra"},
        {"18", "Mojave"},
        {"19", "Catalina"},
        {"20", "Big Sur"},
        {"21", "Monterey"},
        {"22", "Ventura"},
        {"23", "Sonoma"},
        {"24", "Sequoia"},
    };
    constexpr auto PATTERN_MATCH {"[0-9]+"};
    std::string match;
    const std::regex pattern {PATTERN_MATCH};
    const auto ret {Utils::FindRegexInString(in, match, pattern, 0)};

    if (ret)
    {
        const auto it {MAC_CODENAME_MAP.find(match)};
        output["os_codename"] = it != MAC_CODENAME_MAP.end() ? it->second : "";
    }

    return ret;
}
