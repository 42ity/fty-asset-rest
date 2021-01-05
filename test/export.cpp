#include "asset/asset-manager.h"
#include "test-utils.h"

TEST_CASE("Export asset")
{
    fty::asset::db::AssetElement dc = createAsset("datacenter", "Data center", "datacenter");

    SECTION("Export 1")
    {
        fty::asset::db::AssetElement el = createAsset("device", "Device name", "device", dc.id);
        auto exp = fty::asset::AssetManager::exportCsv();
        if (!exp) {
            FAIL(exp.error());
        }
        std::cerr << *exp << std::endl;
        deleteAsset(el);
    }

    SECTION("Export 2")
    {
        fty::asset::db::AssetElement el = createAsset("device", "Device name", "device", dc.id);
        auto exp = fty::asset::AssetManager::exportCsv(dc);
        if (!exp) {
            FAIL(exp.error());
        }
        std::cerr << *exp << std::endl;
        deleteAsset(el);
    }

    deleteAsset(dc);
}
