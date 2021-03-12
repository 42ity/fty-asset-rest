#include "test-utils.h"
#include "asset/asset-manager.h"

TEST_CASE("Create asset")
{
    assets::DataCenter dc("datacenter");
    dc.setExtName("Data center");

    SECTION("Create 1")
    {
        static std::string json = R"({
            "location" :            "Data center",
            "name" :                "dev1",
            "powers":               [],
            "priority" :            "P2",
            "status" :              "active",
            "sub_type" :            "N_A",
            "type" :                "room",
            "ext": [
                {"asset_tag": "", "read_only": false},
                {"contact_name": "", "read_only": false},
                {"contact_email": "", "read_only": false},
                {"contact_phone": "", "read_only": false},
                {"description": "", "read_only": false},
                {"create_mode": "", "read_only": false},
                {"update_ts": "", "read_only": false}
            ]
        })";

        auto ret = fty::asset::AssetManager::createAsset(json, "dummy", false);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(ret);
        CHECK(*ret > 0);
        auto el = fty::asset::db::selectAssetElementWebById(*ret);
        deleteAsset(*el);
    }

    SECTION("Wrong power")
    {
        assets::Feed feed("feed");


        tnt::Connection           conn;
        fty::asset::db::AssetLink link;
        link.dest = feed.id;
        link.src  = feed.id;
        link.type = 1;

        auto ret  = fty::asset::db::insertIntoAssetLink(conn, link);
        REQUIRE(!ret);
        CHECK(ret.error() == "connection loop was detected");

        deleteAsset(feed);
    }

    deleteAsset(dc);
}

TEST_CASE("Feed in same DC")
{
    assets::DataCenter dc1("datacenter1");
    dc1.setExtName("Data center 1");

    assets::DataCenter dc2("datacenter2");
    dc2.setExtName("Data center 2");

    assets::Feed feed1("feed1", dc1);
    feed1.setExtName("feed1");
    assets::Feed feed2("feed2", dc2);
    feed2.setExtName("feed2");

    static std::string okJson = R"({
        "location" :            "Data center 1",
        "name" :                "dev1",
        "powers":               [{"src_id": "feed1", "src_name": "feed1", "src_socket": null}],
        "priority" :            "P2",
        "status" :              "active",
        "sub_type" :            "N_A",
        "type" :                "room"
    })";

    static std::string wrongJson = R"({
        "location" :            "Data center 1",
        "name" :                "dev2",
        "powers":               [{"src_id": "feed2", "src_name": "feed2", "src_socket": null}],
        "priority" :            "P2",
        "status" :              "active",
        "sub_type" :            "N_A",
        "type" :                "room"
    })";

    {
        auto ret = fty::asset::AssetManager::createAsset(okJson, "dummy", false);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(ret);
        CHECK(*ret > 0);
        auto el = fty::asset::db::selectAssetElementWebById(*ret);
        deleteAsset(*el);
    }

    {
        auto ret = fty::asset::AssetManager::createAsset(wrongJson, "dummy", false);
        REQUIRE(!ret);
        CHECK(ret.error().message() == "Request CREATE asset dev2 FAILED: Power source is not in same DC");
    }
}
