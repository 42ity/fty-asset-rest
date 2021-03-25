#pragma once
#include "asset/asset-db.h"
#include "asset/asset-helpers.h"
#include "asset/db.h"
#include <catch2/catch.hpp>
#include <fty_common_asset_types.h>
#include <yaml-cpp/yaml.h>

inline fty::asset::db::AssetElement createAsset(
    const std::string& name, const std::string& extName, const std::string& type, uint32_t parentId = 0)
{
    tnt::Connection conn;

    fty::asset::db::AssetElement el;
    el.name     = name;
    el.status   = "active";
    el.priority = 1;
    el.typeId   = persist::type_to_typeid(type);
    el.parentId = parentId;

    {
        auto ret = fty::asset::db::insertIntoAssetElement(conn, el, true);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
        el.id = *ret;
    }

    {
        auto ret = fty::asset::db::insertIntoAssetExtAttributes(conn, el.id, {{"name", extName}}, true);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
    }

    return el;
}

template <uint16_t Type, uint16_t SubType = 0>
class AssetElement : public fty::asset::db::AssetElement
{
public:
    AssetElement(const std::string& _name)
    {
        name      = _name;
        status    = "active";
        priority  = 1;
        typeId    = Type;
        subtypeId = SubType;
        create();
    }

    AssetElement(const std::string& _name, const fty::asset::db::AssetElement& parent)
    {
        parentId  = parent.id;
        name      = _name;
        status    = "active";
        priority  = 1;
        typeId    = Type;
        subtypeId = SubType;
        create();
    }

    void setExtName(const std::string& extName)
    {
        tnt::Connection conn;
        auto            ret = fty::asset::db::insertIntoAssetExtAttributes(conn, id, {{"name", extName}}, true);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
    }

    void setExtAttributes(const std::map<std::string, std::string>& attr)
    {
        tnt::Connection conn;
        auto            ret = fty::asset::db::insertIntoAssetExtAttributes(conn, id, attr, true);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
    }

    void setParent(const fty::asset::db::AssetElement& parent)
    {
        tnt::Connection conn;
        auto            ret = fty::asset::db::updateAssetElement(conn, id, parent.id, status, priority, assetTag);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
    }

    void setPower(const fty::asset::db::AssetElement& source)
    {
        tnt::Connection           conn;
        fty::asset::db::AssetLink link;
        link.dest = id;
        link.src  = source.id;
        link.type = 1;
        auto ret  = fty::asset::db::insertIntoAssetLink(conn, link);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
    }

    void removePower()
    {
        tnt::Connection conn;

        auto ret = fty::asset::db::deleteAssetLinksTo(conn, id);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
    }

    void setActive(bool active)
    {
        tnt::Connection conn;

        auto ret = fty::asset::db::updateAssetElement(conn, id, parentId, active ? "active" : "nonactive", priority, assetTag);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
    }

private:
    void create()
    {
        tnt::Connection conn;
        auto            ret = fty::asset::db::insertIntoAssetElement(conn, *this, true);
        if (!ret) {
            FAIL(ret.error());
        }
        REQUIRE(*ret > 0);
        id = *ret;
    }

};

namespace assets {
using DataCenter = AssetElement<persist::DATACENTER>;
using Device     = AssetElement<persist::DEVICE>;
using Feed       = AssetElement<persist::DEVICE, persist::FEED>;
using Row        = AssetElement<persist::ROW>;
using Rack       = AssetElement<persist::RACK>;
using Server     = AssetElement<persist::DEVICE, persist::SERVER>;
using Ups        = AssetElement<persist::DEVICE, persist::UPS>;
using Sensor     = AssetElement<persist::DEVICE, persist::SENSOR>;
} // namespace assets

inline void deleteAsset(const fty::asset::db::AssetElement& el)
{
    tnt::Connection conn;

    {
        auto ret = fty::asset::db::deleteAssetExtAttributesWithRo(conn, el.id, true);
        if (!ret) {
            FAIL(ret.error());
        }
        CHECK(ret);
    }

    {
        auto ret = fty::asset::db::deleteAssetExtAttributesWithRo(conn, el.id, false);
        if (!ret) {
            FAIL(ret.error());
        }
        CHECK(ret);
    }

    {
        auto ret = fty::asset::db::deleteMonitorAssetRelationByA(conn, el.id);
        if (!ret) {
            FAIL(ret.error());
        }
        CHECK(ret);
    }

    {
        auto ret = fty::asset::db::deleteAssetElement(conn, el.id);
        if (!ret) {
            FAIL(ret.error());
        }
        CHECK(ret);
        CHECK(*ret > 0);
    }
}

inline YAML::Node reorder(const YAML::Node& node)
{
    YAML::Node ret;
    if (node.IsMap()) {
        ret = YAML::Node(YAML::NodeType::Map);
        std::map<std::string, YAML::Node> map;
        for (const auto& it : node) {
            map[it.first.as<std::string>()] = it.second.IsMap() ? reorder(it.second) : it.second;
        }
        for (const auto& [key, val] : map) {
            ret[key] = val;
        }
    } else if (node.IsSequence()) {
        ret = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& it : node) {
            ret.push_back(it.second.IsMap() ? reorder(it.second) : it.second);
        }
    } else {
        ret = node;
    }
    return ret;
}

inline std::string normalizeJson(const std::string& json)
{
    try {
        YAML::Node yaml = YAML::Load(json);

        if (yaml.IsMap()) {
            yaml = reorder(yaml);
        }

        YAML::Emitter out;
        out << YAML::DoubleQuoted << YAML::Flow << yaml;
        return std::string(out.c_str());
    } catch (const std::exception&) {
        return "";
    }
}
