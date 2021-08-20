#include "list-in.h"
#include <asset/asset-db2.h>
#include <asset/asset-helpers.h>
#include <asset/json.h>
#include <fty/rest/component.h>
#include <fty/string-utils.h>
#include <fty_common_asset_types.h>
#include <fty_common_db_connection.h>
#include <pack/node.h>


namespace fty::asset {

// =========================================================================================================================================

struct Asset : public pack::Node
{
    pack::String id      = FIELD("id");
    pack::String name    = FIELD("name");
    pack::String type    = FIELD("type");
    pack::String subType = FIELD("sub_type");

    using pack::Node::Node;
    META(Asset, id, name, type, subType);
};

using Assets = pack::ObjectList<Asset>;

// =========================================================================================================================================

struct AssetDetail : public pack::Node
{
    struct Power : public pack::Node
    {
        pack::String srcName    = FIELD("src_name");
        pack::String srcId      = FIELD("src_id");
        pack::String srcSocket  = FIELD("src_socket");
        pack::String destSocket = FIELD("dest_socket");

        using pack::Node::Node;
        META(Power, srcName, srcId, srcSocket, destSocket);
    };

    struct Outlet : public pack::Node
    {
        pack::String name     = FIELD("name");
        pack::String value    = FIELD("value");
        pack::String readOnly = FIELD("read_only");

        using pack::Node::Node;
        META(Outlet, name, value, readOnly);
    };

    using OutletList = pack::ObjectList<Outlet>;

    pack::String                      id           = FIELD("id");
    pack::String                      pdsInUri     = FIELD("power_devices_in_uri");
    pack::String                      name         = FIELD("name");
    pack::String                      status       = FIELD("status");
    pack::String                      priority     = FIELD("priority");
    pack::String                      type         = FIELD("type");
    pack::String                      locationUri  = FIELD("location_uri");
    pack::String                      locationId   = FIELD("location_id");
    pack::String                      location     = FIELD("location");
    pack::String                      locationType = FIELD("location_type");
    pack::String                      subType      = FIELD("sub_type");
    pack::ObjectList<Power>           powers       = FIELD("powers");
    pack::ObjectList<pack::StringMap> ext          = FIELD("ext");
    pack::StringList                  ips          = FIELD("ips");
    pack::Map<OutletList>             outlets      = FIELD("outlets");

    using pack::Node::Node;
    // clang-format off
    META(AssetDetail, id, pdsInUri, name, status, priority, type, locationUri, locationId, location, locationType, subType, powers, ext,
        ips, outlets);
    // clang-format on
};

using AssetDetails = pack::ObjectList<AssetDetail>;

// =========================================================================================================================================

static Assets assetsInContainer(
    fty::db::Connection& conn, uint32_t container, const db::asset::select::Filter& filter, const std::vector<std::string>& capabilities)
{
    Assets result;

    auto func = [&](const fty::db::Row& row) {
        uint32_t asset_id = row.get<uint32_t>("asset_id");

        // search capabilities if present in filter
        auto keyTag = [&](const std::string& value) {
            std::string capability = fmt::format("capability.{}", value);
            if (auto ret = db::asset::countKeytag(conn, capability, "yes", asset_id)) {
                return *ret > 0;
            } else {
                return false;
            }
        };

        auto itCap = std::find_if_not(capabilities.begin(), capabilities.end(), keyTag);

        // add element if no capabilities present or if capabilites present and all of them not found
        if (capabilities.empty() || itCap == capabilities.end()) {
            auto assetNames = db::asset::idToNameExtName(conn, row.get<uint32_t>("asset_id"));
            if (!assetNames) {
                throw rest::errors::Internal("Database failure"_tr);
            }

            auto& asset = result.append();

            asset.id      = row.get("name");
            asset.name    = assetNames->extName;
            asset.type    = persist::typeid_to_type(row.get<uint16_t>("type_id"));
            asset.subType = persist::subtypeid_to_subtype(row.get<uint16_t>("subtype_id"));
        }
    };

    if (container) {
        auto list = db::asset::select::itemsByContainer(conn, container, func, filter);
        if (!list) {
            throw rest::errors::Internal(list.error());
        }
    } else {
        auto list = db::asset::select::items(conn, func, filter);
        if (!list) {
            throw rest::errors::Internal(list.error());
        }
    }
    return result;
}

// =========================================================================================================================================

struct Outlet
{
    db::asset::ExtAttrValue label;
    db::asset::ExtAttrValue type;
    db::asset::ExtAttrValue group;
};

static std::string getOutletNumber(const std::string& extAttributeName)
{
    auto        dot1    = extAttributeName.find_first_of(".");
    std::string oNumber = extAttributeName.substr(dot1 + 1);
    auto        dot2    = oNumber.find_first_of(".");
    oNumber             = oNumber.substr(0, dot2);
    return oNumber;
}

static std::map<std::string, Outlet> collectOutlets(const db::asset::Attributes& ext)
{
    static std::regex outletLabelRex("^outlet\\.[0-9][0-9]*\\.label$");
    static std::regex outletGroupRex("^outlet\\.[0-9][0-9]*\\.group$");
    static std::regex outletTypeRex("^outlet\\.[0-9][0-9]*\\.type$");
    static std::regex outletSwitchable("^outlet\\.[0-9][0-9]*\\.switchable");

    std::map<std::string, Outlet> outlets;

    for (const auto& [key, value] : ext) {
        if (key.find("outlet.") != 0) {
            continue;
        }

        if (std::regex_match(key, outletLabelRex)) {
            auto   oNumber = getOutletNumber(key);
            Outlet out;
            out.label.value    = key;
            out.label.readOnly = value.readOnly;
            outlets[oNumber]   = out;
            continue;
        } else if (std::regex_match(key, outletGroupRex)) {
            auto   oNumber = getOutletNumber(key);
            Outlet out;
            out.group.value    = key;
            out.group.readOnly = value.readOnly;
            outlets[oNumber]   = out;
            continue;
        } else if (std::regex_match(key, outletTypeRex)) {
            auto   oNumber = getOutletNumber(key);
            Outlet out;
            out.type.value    = key;
            out.type.readOnly = value.readOnly;
            outlets[oNumber]  = out;
            continue;
        } else if (std::regex_match(key, outletSwitchable)) {
            auto oNumber     = getOutletNumber(key);
            outlets[oNumber] = {};
            continue;
        } else if (key == "outlet.switchable") {
            if (!outlets.count("0")) {
                outlets["0"] = {};
                continue;
            }
        } else if (key == "outlet.label") {
            Outlet out;
            out.label.value    = key;
            out.label.readOnly = value.readOnly;
            outlets["0"]       = out;
        }
    }

    return outlets;
}

static void fetchFullInfo(fty::db::Connection& conn, AssetDetail& asset, const std::string& id)
{
    auto info = db::asset::select::itemExt(conn, id);
    if (!info) {
        throw rest::errors::Internal(info.error());
    }

    db::asset::Attributes ext;
    if (auto ret = db::asset::select::extAttributes(conn, info->id); !ret) {
        throw rest::errors::Internal(ret.error());
    } else {
        ext = *ret;
    }

    auto outlets = collectOutlets(ext);

    asset.id       = info->name;
    asset.name     = info->extName;
    asset.status   = info->status;
    asset.priority = fmt::format("P{}", info->priority);
    asset.type     = info->typeName;
    if (info->parentId > 0) {
        auto location = db::asset::idToNameExtName(conn, info->parentId);
        if (!location) {
            throw rest::errors::Internal(location.error());
        }
        asset.locationUri  = fmt::format("/api/v1/asset/{}", location->name);
        asset.locationId   = location->name;
        asset.location     = location->extName;
        asset.locationType = persist::typeid_to_type(info->parentTypeId);
    }

    {
        std::string subTypeName;
        if (info->typeName == "group") {
            if (ext.count("type")) {
                subTypeName = ext["type"].value;
                ext.erase("type");
            }
        } else {
            subTypeName = persist::subtypeid_to_subtype(info->subtypeId);
        }
        if (subTypeName == "N_A") {
            subTypeName = "";
        }
        asset.subType = subTypeName;
    }

    if (auto links = db::asset::select::deviceLinksTo(conn, info->id)) {
        for (const auto& link : *links) {
            if (auto extname = db::asset::nameToExtName(conn, link.srcName)) {
                auto& power      = asset.powers.append();
                power.srcId      = link.srcName;
                power.srcName    = *extname;
                power.srcSocket  = link.srcSocket;
                power.destSocket = link.destSocket;
            } else {
                throw rest::errors::Internal(extname.error());
            }
        }
    } else {
        throw rest::errors::Internal(links.error());
    }

    {
        auto it = ext.find("logical_asset");
        if (it != ext.end()) {
            auto extname = db::asset::nameToExtName(conn, it->second.value);
            if (!extname) {
                throw rest::errors::Internal(extname.error());
            }
            ext["logical_asset"] = {*extname, it->second.readOnly};
        }
    }

    if (!info->assetTag.empty()) {
        auto& tag = asset.ext.append();
        tag.append("asset_tag", info->assetTag);
        tag.append("read_only", "false");
    }

    for (const auto& [key, value] : ext) {
        if (key == "name" || key == "location_type") {
            continue;
        }

        if (key.find("ip.") == 0) {
            asset.ips.append(value.value);
        }

        auto& attr = asset.ext.append();
        attr.append(key, value.value);
        attr.append("read_only", convert<std::string>(value.readOnly));
    }

    for (const auto& [oNumber, outlet] : outlets) {
        AssetDetail::OutletList list = asset.outlets.append(oNumber);

        {
            auto& out = list.append();
            out.name  = "label";
            if (!outlet.label.value.empty()) {
                out.value    = outlet.label.value;
                out.readOnly = convert<std::string>(outlet.label.readOnly);
            } else {
                out.value    = oNumber;
                out.readOnly = "true";
            }
        }

        if (!outlet.group.value.empty()) {
            auto& out    = list.append();
            out.name     = "group";
            out.value    = outlet.group.value;
            out.readOnly = convert<std::string>(outlet.group.readOnly);
        }

        if (!outlet.type.value.empty()) {
            auto& out    = list.append();
            out.name     = "type";
            out.value    = outlet.type.value;
            out.readOnly = convert<std::string>(outlet.type.readOnly);
        }
    }
}

// =========================================================================================================================================

uint32_t ListIn::containerId() const
{
    auto id = m_request.queryArg<std::string>("in");
    if (!id) {
        throw rest::errors::RequestParamRequired("in");
    }

    if (id->empty()) {
        return 0;
    }

    if (auto ret = checkElementIdentifier("in", *id)) {
        return *ret;
    } else {
        throw rest::errors::RequestParamBad("in", *id, "valid container id");
    }
}

std::vector<uint16_t> ListIn::types() const
{
    std::vector<uint16_t> ret;

    auto type = m_request.queryArg<std::string>("type");

    if (type && !type->empty()) {
        std::vector<std::string> items = split(*type, ",");
        for (const auto& it : items) {
            if (auto retType = persist::type_to_typeid(it); !retType) {
                throw rest::errors::RequestParamBad("type", *type, "valid type like datacenter, room, etc..."_tr);
            } else {
                ret.emplace_back(retType);
            }
        }
    }

    return ret;
}

std::vector<uint16_t> ListIn::subTypes() const
{
    std::vector<uint16_t> ret;

    auto subs = m_request.queryArg<std::string>("sub_type");

    if (subs && !subs->empty()) {
        std::vector<std::string> items = split(*subs, ",");
        for (const auto& it : items) {
            if (auto sub = persist::subtype_to_subtypeid(it); !sub) {
                throw rest::errors::RequestParamBad("subtype", *subs, "valid sub_type like feed, ups, etc..."_tr);
            } else {
                ret.emplace_back(sub);
            }
        }
    }

    return ret;
}

std::vector<std::string> ListIn::capabilities() const
{
    std::vector<std::string> ret;

    auto capability = m_request.queryArg<std::string>("capability");

    if (capability && !capability->empty()) {
        ret = split(*capability, ",");
    }

    return ret;
}

unsigned ListIn::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    if (m_request.type() != rest::Request::Type::Get) {
        throw rest::errors::MethodNotAllowed(m_request.typeStr());
    }

    auto details = m_request.queryArg<bool>("details");

    db::Connection conn;

    db::asset::select::Filter flt;
    flt.types    = types();
    flt.subtypes = subTypes();
    if (auto without = m_request.queryArg<std::string>("without")) {
        flt.without = *without;
    }
    if (auto status = m_request.queryArg<std::string>("status")) {
        flt.status = status;
    }

    auto assets = assetsInContainer(conn, containerId(), flt, capabilities());

    if (details && *details) {
        AssetDetails list;
        for (auto const& asset : assets) {
            auto& detail = list.append();
            fetchFullInfo(conn, detail, asset.id);
        }
        m_reply << *pack::json::serialize(list, pack::Option::WithDefaults);
    } else {
        m_reply << *pack::json::serialize(assets);
    }

    return HTTP_OK;
}

// =========================================================================================================================================

} // namespace fty::asset

registerHandler(fty::asset::ListIn)
