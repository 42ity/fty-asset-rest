#include "asset/asset-import.h"
#include "asset/asset-manager.h"
#include "asset/csv.h"
#include "asset/logger.h"

#define CREATE_MODE_CSV 2

namespace fty::asset {

AssetExpected<AssetManager::ImportList> AssetManager::importCsv(
    const std::string& csvStr, const std::string& user, bool sendNotify)
{
    std::stringstream ss(csvStr);
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
