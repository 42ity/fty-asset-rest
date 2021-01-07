#include "test-utils.h"
#include "asset/asset-db.h"
#include "asset/asset-manager.h"

struct Holder
{
    Holder(uint32_t id):
        m_el(*fty::asset::db::selectAssetElementWebById(id))
    {}

    Holder(const fty::asset::db::AssetElement& el):
        m_el(el)
    {}

    ~Holder()
    {
        std::cerr << "delete " << m_el.name << std::endl;
        deleteAsset(m_el);
    }

    fty::asset::db::AssetElement m_el;
};

static Holder placeAsset(int size, int location)
{
    using namespace fmt::literals;
    std::string json;
    try {
        json = fmt::format(R"({{
            "location" :            "Rack",
            "name" :                "{name}",
            "priority" :            "P2",
            "status" :              "active",
            "sub_type" :            "server",
            "type" :                "device",
            "ext": [
                {{"u_size"         : "{size}",      "read_only": true}},
                {{"location_u_pos" : "{loc}",       "read_only": true}},
                {{"location_w_pos" : "horizonatal", "read_only": true}}
            ]
        }})", "name"_a = "el"+std::to_string(random()), "size"_a = size, "loc"_a = location);
    } catch(const fmt::format_error& e) {
        std::cerr << e.what() << std::endl;
    }

    auto ret = fty::asset::AssetManager::createAsset(json, "dummy", false);
    if (!ret) {
        FAIL(ret.error());
    }
    REQUIRE(ret);
    CHECK(*ret > 0);
    return Holder(*ret);
}

TEST_CASE("USize/Ok")
{
    fty::asset::db::AssetElement dc   = createAsset("datacenter", "Data center", "datacenter");
    fty::asset::db::AssetElement row  = createAsset("row", "Row", "row", dc.id);
    fty::asset::db::AssetElement rack = createAsset("rack", "Rack", "rack", row.id);

    tnt::Connection conn;
    fty::asset::db::insertIntoAssetExtAttributes(conn, rack.id, {{"u_size", "10"}}, true);

    SECTION("Fit it")
    {
        auto el = placeAsset(2, 1);
        auto el1 = placeAsset(2, 2);
        auto el2 = placeAsset(2, 3);
        auto el3 = placeAsset(2, 4);
        std::cerr << "exit scope" << std::endl;
    }

    deleteAsset(rack);
    deleteAsset(row);
    deleteAsset(dc);
}


