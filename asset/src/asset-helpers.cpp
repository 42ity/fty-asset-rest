#include "asset/asset-helpers.h"
#include "asset/asset-db.h"
#include <ctime>

namespace fty::asset {

AssetExpected<uint32_t> checkElementIdentifier(const std::string& paramName, const std::string& paramValue)
{
    assert(!paramName.empty());
    if (paramValue.empty()) {
        return unexpected(error(Errors::ParamRequired).format(paramName));
    }

    static std::string prohibited = "_@%;\"";

    for (auto a : prohibited) {
        if (paramValue.find(a) != std::string::npos) {
            std::string err      = "value '{}' contains prohibited characters ({})"_tr.format(paramValue, prohibited);
            std::string expected = "valid identifier"_tr;
            return unexpected(error(Errors::BadParams).format(paramName, err, expected));
        }
    }

    if (auto eid = db::nameToAssetId(paramValue)) {
        return uint32_t(*eid);
    } else {
        std::string err      = "value '{}' is not valid identifier. Error: {}"_tr.format(paramValue, eid.error());
        std::string expected = "existing identifier"_tr;
        return unexpected(error(Errors::BadParams).format(paramName, err, expected));
    }
}

AssetExpected<std::string> sanitizeDate(const std::string& inp)
{
    static std::vector<std::string> formats = {"%d-%m-%Y", "%Y-%m-%d", "%d-%b-%y", "%d.%m.%Y", "%d %m %Y", "%m/%d/%Y"};

    struct tm timeinfo;
    for (const auto& fmt : formats) {
        if (!strptime(inp.c_str(), fmt.c_str(), &timeinfo)) {
            continue;
        }
        std::array<char, 11> buff;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        std::strftime(buff.data(), buff.size(), fmt.c_str(), &timeinfo);
#pragma GCC diagnostic pop
        return std::string(buff.begin(), buff.end());
    }

    return unexpected("Not is ISO date"_tr);
}

AssetExpected<double> sanitizeValueDouble(const std::string& key, const std::string& value)
{
    try {
        std::size_t pos     = 0;
        double      d_value = std::stod(value, &pos);
        if (pos != value.length()) {
            return unexpected(error(Errors::BadParams).format(key, value, "value should be a number"_tr));
        }
        return d_value;
    } catch (const std::exception&) {
        return unexpected(error(Errors::BadParams).format(key, value, "value should be a number"_tr));
    }
}

} // namespace fty::asset
