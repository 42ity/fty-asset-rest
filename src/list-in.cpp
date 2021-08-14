#include "list-in.h"
#include <asset/asset-db.h>
#include <asset/asset-helpers.h>
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

static Assets assetsInContainer(
    uint32_t                        container,
    const std::vector<uint16_t>&    types,
    const std::vector<uint16_t>&    subtypes,
    const std::vector<std::string>& capabilities,
    const std::string&              without,
    const std::string&              status)
{
    fty::db::Connection conn;

    Assets result;
    auto   func = [&](const fty::db::Row& row) {
        uint32_t asset_id = row.get<uint32_t>("asset_id");

        // search capabilities if present in filter
        auto keyTag = [&](const std::string& value) {
            std::string capability = fmt::format("capability.{}", value);
            if (auto ret = db::hasAssetKeytagValue(conn, asset_id, capability, "yes")) {
                return *ret > 0;
            } else {
                return false;
            }
        };

        auto itCap = std::find_if_not(capabilities.begin(), capabilities.end(), keyTag);

        // add element if no capabilities present or if capabilites present and all of them not found
        if (capabilities.size() == 0 || itCap == capabilities.end()) {
            auto assetNames = db::idToNameExtName(row.get<uint32_t>("asset_id"));
            if (!assetNames) {
                throw rest::errors::Internal("Database failure"_tr);
            }

            auto& asset = result.append();

            asset.id      = row.get("name");
            asset.name    = assetNames->second;
            asset.type    = persist::typeid_to_type(row.get<uint16_t>("type_id"));
            asset.subType = persist::subtypeid_to_subtype(row.get<uint16_t>("subtype_id"));
        }
    };

    if (container) {
        auto list = db::selectAssetsByContainer(conn, container, types, subtypes, without, status, func);
        if (!list) {
            throw rest::errors::Internal(list.error());
        }
    } else {
        auto list = db::selectAssetsAllContainer(conn, types, subtypes, without, status, func);
        if (!list) {
            throw rest::errors::Internal(list.error());
        }
    }
    return result;
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

    auto without = m_request.queryArg<std::string>("without");
    auto status  = m_request.queryArg<std::string>("status");
    auto details = m_request.queryArg<bool>("details");


    auto assets = assetsInContainer(containerId(), types(), subTypes(), capabilities(), without ? *without : "", status ? *status : "");
    // m_reply << *pack::json::serialize(res);

    if (details && *details) {
        std::set<std::string> listElements;
        for (auto const& asset : assets) {
            listElements.insert(asset.id);
        }
        std::stringstream ss;
        persist::export_asset_json(ss, &listElements);
        m_reply << ss.str();
    } else {
        m_reply << *pack::json::serialize(assets);
    }

    return HTTP_OK;
}

// =========================================================================================================================================

} // namespace fty::asset

registerHandler(fty::asset::ListIn)
