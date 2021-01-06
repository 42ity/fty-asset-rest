#pragma once

#include "asset-db.h"
#include "error.h"
#include <fty/expected.h>
#include <fty/translate.h>
#include <fty_common_db_asset.h>
#include <fty_common_db_exception.h>
#include <map>
#include <string>

namespace fty::asset {

class AssetManager
{
public:
    using AssetList  = std::map<uint32_t, std::string>;
    using ImportList = std::map<size_t, Expected<uint32_t>>;

public:
    static AssetExpected<db::WebAssetElementExt> getItem(uint32_t id);

    static AssetExpected<AssetList> getItems(const std::string& typeName, const std::string& subtypeName);

    static AssetExpected<db::AssetElement> deleteAsset(uint32_t id, bool sendNotify = true);

    static std::map<std::string, AssetExpected<db::AssetElement>> deleteAsset(
        const std::map<uint32_t, std::string>& ids, bool sendNotify = true);

    static AssetExpected<db::AssetElement> deleteAsset(const db::AssetElement& element, bool sendNotify = true);

    static AssetExpected<uint32_t> createAsset(
        const std::string& json, const std::string& user, bool sendNotify = true);

    static AssetExpected<ImportList> importCsv(const std::string& csv, const std::string& user, bool sendNotify = true);
    static AssetExpected<std::string> exportCsv(const std::optional<db::AssetElement>& dc = std::nullopt);

private:
    static AssetExpected<db::AssetElement> deleteDcRoomRowRack(const db::AssetElement& element);
    static AssetExpected<db::AssetElement> deleteGroup(const db::AssetElement& element);
    static AssetExpected<db::AssetElement> deleteDevice(const db::AssetElement& element);
};

} // namespace fty::asset
