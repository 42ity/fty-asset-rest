#include "asset/asset-manager.h"
#include "test-utils.h"

TEST_CASE("Export asset")
{
    fty::asset::db::AssetElement dc = createAsset("datacenter", "Data center", "datacenter");

    SECTION("Export 1")
    {
        assets::Device dev("device", dc);

        auto exp = fty::asset::AssetManager::exportCsv();
        if (!exp) {
            FAIL(exp.error());
        }
        std::cerr << *exp << std::endl;

        deleteAsset(dev);
    }

    SECTION("Wrong order")
    {
        assets::Device dev1("dev1", dc);
        assets::Device dev2("dev2", dc);

        assets::Rack rc("rack", dc);
        rc.setExtName("Rack");

        dev1.setParent(rc);

        auto exp = fty::asset::AssetManager::exportCsv(dc);
        if (!exp) {
            FAIL(exp.error());
        }
        std::cerr << *exp << std::endl;

        deleteAsset(dev1);
        deleteAsset(dev2);
        deleteAsset(rc);
    }

    SECTION("Wrong power order")
    {
        assets::Device dev1("dev1", dc);
        assets::Device dev2("dev2", dc);

        assets::Feed feed("feed", dc);
        feed.setExtName("Feed");

        dev1.setPower(feed);

        auto exp = fty::asset::AssetManager::exportCsv(dc);
        if (!exp) {
            FAIL(exp.error());
        }
        std::cerr << *exp << std::endl;

        dev1.removePower();
        deleteAsset(dev1);
        deleteAsset(dev2);
        deleteAsset(feed);
    }

    deleteAsset(dc);
}
