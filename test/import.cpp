#include "asset/asset-manager.h"
#include "test-utils.h"

TEST_CASE("Import asset")
{
    static std::string data = R"(name,type,sub_type,location,status,priority,asset_tag,power_source.1,power_plug_src.1,power_input.1,description,ip.1,company,site_name,region,country,address,contact_name,contact_email,contact_phone,u_size,manufacturer,model,serial_no,runtime,installation_date,maintenance_date,maintenance_due,location_u_pos,location_w_pos,end_warranty_date,hostname.1,http_link.1,accordion_is_open,asset_order,logical_asset,max_power,phases.output,port,group.1,id
Washington DC,datacenter,,,active,P1,ACDC007,,,,,,,,,,"Washington, Washington, USA, Earth, Orion galaxy",,,,,,,,,,,,,,,,,,,,1000,,,,
Room1,room,,Washington DC,active,P1,,,,,,,,,,,,,,,,,,,,,,,,,,,,true,1,,,,,,
Row1,row,,Room1,active,P1,,,,,,,,,,,,,,,,,,,,,,,,,,,,,1,,,,,,
Rack1,rack,,Row1,active,P1,,,,,,,,,,,,,,,42,,,,,,,,,,,,,,1,,,,,,
NewsFeed,device,feed,Washington DC,active,P1,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,1,,,)";

    auto ret = fty::asset::AssetManager::importCsv(data, "dummy", false);
    if (!ret) {
        FAIL(ret.error());
    }
    REQUIRE(ret);
    for (auto iter = ret->rbegin(); iter != ret->rend(); ++iter) {
        if (!iter->second) {
            FAIL(iter->second.error());
        }
        auto el = fty::asset::db::selectAssetElementWebById(*(iter->second));
        deleteAsset(*el);
    }
}

TEST_CASE("Import asset 2")
{
    static std::string data = R"(name,type,sub_type,location,status,priority,asset_tag,power_source.1,power_plug_src.1,power_input.1,description,ip.1,company,site_name,region,country,address,contact_name,contact_email,contact_phone,u_size,manufacturer,model,serial_no,runtime,installation_date,maintenance_date,maintenance_due,location_u_pos,location_w_pos,end_warranty_date,hostname.1,http_link.1,accordion_is_open,asset_order,logical_asset,max_power,phases.output,port,group.1,id
Washington 'DC',datacenter,,,active,P1,ACDC007,,,,,,,,,,"Washington, Washington, USA, Earth, Orion galaxy",,,,,,,,,,,,,,,,,,,,1000,,,,
Room1,room,,Washington 'DC',active,P1,,,,,,,,,,,,,,,,,,,,,,,,,,,,true,1,,,,,,)";

    auto ret = fty::asset::AssetManager::importCsv(data, "dummy", false);
    if (!ret) {
        FAIL(ret.error());
    }
    REQUIRE(ret);
    for (auto iter = ret->rbegin(); iter != ret->rend(); ++iter) {
        if (!iter->second) {
            FAIL(iter->second.error());
        }
        auto el = fty::asset::db::selectAssetElementWebById(*(iter->second));
        deleteAsset(*el);
    }
}


