#include "asset/asset-import.h"
#include "asset/asset-manager.h"
#include "asset/csv.h"
#include "asset/logger.h"
#include <fty/split.h>

#define CREATE_MODE_CSV 2

namespace fty::asset {

static std::string sanitizeCol(const std::string& col)
{
    if (col.empty()) {
        return col;
    }

    if (col[0] == col[col.size()-1] && (col[0] == '\'' || col[0] == '"')) {
        return col;
    }

    std::regex re("\'|\"");
    return std::regex_replace(col, re, "\\$&");
}

static std::string sanitize(const std::string& csvStr)
{
    std::vector<std::string> out;
    auto rows = split(csvStr, std::regex("\r\n"), SplitOption::KeepEmpty);
    for(const auto& row: rows) {
        char inQuota = 0;
        std::string col;
        std::vector<std::string> outRow;
        for(size_t i = 0; i < row.length(); ++i) {
            char it = row[i];
            if ((it == '\'' || it == '"') && ((i > 0 && row[i-1] != '\\') || i == 0)) {
                if (!inQuota) {
                    inQuota = it;
                } else if (inQuota == it) {
                    inQuota = 0;
                }
            }
            if (it == ',' && !inQuota) {
                outRow.push_back(sanitizeCol(col));
                col.clear();
                continue;
            }
            col += it;
        }
        outRow.push_back(col);
        out.push_back(implode(outRow, ","));
    }
    return implode(out, "\n");
}

AssetExpected<AssetManager::ImportList> AssetManager::importCsv(
    const std::string& csvStr, const std::string& user, bool sendNotify)
{
    std::stringstream ss(sanitize(csvStr));
    CsvMap            csv = CsvMap_from_istream(ss);

    csv.setCreateMode(CREATE_MODE_CSV);
    csv.setCreateUser(user);
    csv.setUpdateUser(user);

    Import import(csv);
    if (auto ret = import.process(sendNotify)) {
        AssetManager::ImportList res;
        const auto& list = import.items();
        for(const auto&[row, el]: list) {
            if (el) {
                res.emplace(row, el->id);
            } else {
                res.emplace(row, unexpected(el.error()));
            }
        }
        return std::move(res);
    } else {
        return unexpected(ret.error());
    }
}

} // namespace fty::asset
