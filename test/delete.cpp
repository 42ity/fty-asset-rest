#include "asset/json.h"
#include "asset/asset-manager.h"
#include "test-utils.h"

TEST_CASE("Delete asset")
{
    std::cerr << "begin" << std::endl;
    assets::DataCenter dc("datacenter");
    dc.setExtAttributes({{"fast_track", "true"}});

    SECTION("Last feed")
    {
        assets::Feed feed("feed", dc);

        auto ret = fty::asset::AssetManager::deleteAsset(feed, false);
        REQUIRE(!ret);
        CHECK("Last feed cannot be deleted in device centric mode" == ret.error().toString());
        deleteAsset(feed);
    }

    SECTION("Last feeds")
    {
        assets::Feed feed1("feed1", dc);
        assets::Feed feed2("feed2", dc);

        {
            auto ret = fty::asset::AssetManager::deleteAsset(feed1, false);
            if (!ret) {
                FAIL(ret.error());
            }
            REQUIRE(ret);
        }

        {
            auto ret = fty::asset::AssetManager::deleteAsset(feed2, false);
            REQUIRE(!ret);
            CHECK("Last feed cannot be deleted in device centric mode" == ret.error().toString());
            deleteAsset(feed2);
        }
    }

    SECTION("Last feeds 2")
    {
        assets::Feed feed1("feed1", dc);
        assets::Feed feed2("feed2", dc);

        auto ret = fty::asset::AssetManager::deleteAsset({{feed1.id, feed1.name}, {feed2.id, feed2.name}}, false);
        REQUIRE(ret.size() == 2);
        CHECK(ret.at(feed1.name).isValid());
        CHECK(!ret.at(feed2.name).isValid());
        CHECK("Last feed cannot be deleted in device centric mode" == ret.at(feed2.name).error().toString());
        deleteAsset(feed2);
    }

    deleteAsset(dc);
}
