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
    fty::db::Connection&             conn,
    uint32_t                         container,
    const db::asset::select::Filter& filter,
    const db::asset::select::Order&  order,
    const std::vector<std::string>&  capabilities)
{
    Assets result;

    auto func = [&](const fty::db::Row& row) {
        uint32_t assetId = row.get<uint32_t>("id");

        // search capabilities if present in filter
        auto keyTag = [&](const std::string& value) {
            std::string capability = fmt::format("capability.{}", value);
            if (auto ret = db::asset::countKeytag(conn, capability, "yes", assetId)) {
                return *ret > 0;
            } else {
                return false;
            }
        };

        auto itCap = std::find_if_not(capabilities.begin(), capabilities.end(), keyTag);

        // add element if no capabilities present or if capabilites present and all of them not found
        if (capabilities.empty() || itCap == capabilities.end()) {
            auto& asset = result.append();

            asset.id      = row.get("name");
            asset.name    = row.get("extName");
            asset.type    = row.get("typeName");
            asset.subType = row.get("subTypeName");
        }
    };

    if (container) {
        auto list = db::asset::select::itemsByContainer(conn, container, func, filter, order);
        if (!list) {
            throw rest::errors::Internal(list.error());
        }
    } else {
        auto list = db::asset::select::items(conn, func, filter, order);
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
    db::asset::ExtAttrValue name;
    db::asset::ExtAttrValue switchable;
};

// parse ext. attribute as "outlet.<id>.<property-name>
// with exceptions for master outlet (ups/epdu)
// returns <outlet id, property name> (empty if inconsistent)

static std::pair<std::string, std::string> getOutletIdAndProperty(const std::string& extAttributeName)
{
    // exception: handle outlet.["id"|"switchable"|"label"] (ups/epdu)
    // ZZZ assume master outletId is "0"
    if (extAttributeName == "outlet.id") {
        return {"0", "id"};
    }
    if (extAttributeName == "outlet.switchable") {
        return {"0", "switchable"};
    }
    if (extAttributeName == "outlet.label") {
        return {"0", "label"};
    }
    //

    if (extAttributeName.find("outlet.") != 0) {
        return {}; // empty
    }
    auto dot = extAttributeName.find_first_of(".");
    if (dot == std::string::npos) {
        return {}; // empty
    }
    std::string aux = extAttributeName.substr(dot + 1);
    dot = aux.find_first_of(".");
    if (dot == std::string::npos) {
        return {}; // empty
    }

    auto outletId = aux.substr(0, dot);
    auto propertyName = aux.substr(dot + 1);

    try {
        auto i = std::stoi(outletId);
        if (i <= 0) {
            return {}; // inconsistent (>0 required)
        }
    }
    catch (...) {
        return {}; // not an int
    }

    return {outletId, propertyName};
}

static std::map<std::string, Outlet> collectOutlets(const db::asset::Attributes& ext)
{
    std::map<std::string, Outlet> outlets;

    for (const auto& [key, value] : ext) {
        // key match "outlet.<id>.<property-name>"?
        auto pair = getOutletIdAndProperty(key);
        auto outletId = pair.first;
        auto propertyName = pair.second;
        if (outletId.empty() || propertyName.empty()) {
            continue; // don't match
        }

        if (outlets.find(outletId) == outlets.end()) {
            outlets[outletId] = Outlet{}; // create
            outlets[outletId].label.value = outletId; // default required
            outlets[outletId].label.readOnly = true;
        }

        if (propertyName == "label") {
            outlets[outletId].label = value;
        }
        else if (propertyName == "group") {
            outlets[outletId].group = value;
        }
        else if (propertyName == "type") {
            outlets[outletId].type = value;
        }
        else if (propertyName == "name") {
            outlets[outletId].name = value;
        }
        else if (propertyName == "switchable") {
            outlets[outletId].switchable = value;
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
            subTypeName = info->subtypeName;
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
            continue;
        }

        auto& attr = asset.ext.append();
        attr.append(key, value.value);
        attr.append("read_only", convert<std::string>(value.readOnly));
    }

    for (const auto& [oNumber, outlet] : outlets) {
        // exception: ignore outlet "0" for epdu (not a physical outlet)
        if ((oNumber == "0") && (asset.subType == "epdu")) {
            continue;
        }

        AssetDetail::OutletList& list = asset.outlets.append(oNumber);

        // ensure outlet label is defined (required)
        {
            auto& out = list.append();
            out.name  = "label";
            if (!outlet.label.value.empty()) {
                out.value = outlet.label.value;
                out.readOnly = convert<std::string>(outlet.label.readOnly);
            }
            else {
                out.value = oNumber;
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
        if (!outlet.name.value.empty()) {
            auto& out    = list.append();
            out.name     = "name";
            out.value    = outlet.name.value;
            out.readOnly = convert<std::string>(outlet.name.readOnly);
        }
        if (!outlet.switchable.value.empty()) {
            auto& out    = list.append();
            out.name     = "switchable";
            out.value    = outlet.switchable.value;
            out.readOnly = convert<std::string>(outlet.switchable.readOnly);
        }
    }
}

// =========================================================================================================================================

uint32_t ListIn::containerId() const
{
    auto id = m_request.queryArg<std::string>("in");

    if (!id || id->empty()) {
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
        flt.status = *status;
    }

    db::asset::select::Order order;
    if (auto by = m_request.queryArg<std::string>("orderBy")) {
        order.field = *by;
    }
    if (auto dir = m_request.queryArg<std::string>("order")) {
        order.dir = *dir == "ASC" ? db::asset::select::Order::Dir::Asc : db::asset::select::Order::Dir::Desc;
    }


    auto assets = assetsInContainer(conn, containerId(), flt, order, capabilities());

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
