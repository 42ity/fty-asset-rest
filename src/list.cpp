#include "list.h"
#include <asset/asset-db.h>
#include <asset/asset-manager.h>
#include <fty/rest/component.h>
#include <fty/string-utils.h>
#include <fty_common_asset_types.h>
#include <pack/pack.h>

namespace fty::asset {

struct Info : public pack::Node
{
    pack::UInt32 id   = FIELD("id");
    pack::String name = FIELD("name");

    using pack::Node::Node;
    META(Info, id, name);
};

unsigned List::run()
{
    static const std::set<std::string> possibleOrders = {
        "name",
        "model",
        "create_ts",
        "firmware",
        "max_power",
        "serial_no",
        "update_ts",
        "asset_order"
    };

    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    if (m_request.type() != rest::Request::Type::Get) {
        throw rest::errors::MethodNotAllowed(m_request.typeStr());
    }

    Expected<std::string> assetType = m_request.queryArg<std::string>("type");
    Expected<std::string> subtype   = m_request.queryArg<std::string>("subtype");
    Expected<std::string> orderBy   = m_request.queryArg<std::string>("order_by");
    Expected<std::string> orderDir  = m_request.queryArg<std::string>("order");

    if (!assetType) {
        throw rest::errors::RequestParamRequired("type");
    }

    if (!persist::type_to_typeid(*assetType)) {
        throw rest::errors::RequestParamBad("type", *assetType, "datacenter/room/row/rack/group/device");
    }

    std::vector<std::string> subtypes;
    if (subtype) {
        subtypes = split(*subtype, ",");
        for (const auto& it : subtypes) {
            if (!persist::subtype_to_subtypeid(it)) {
                throw rest::errors::RequestParamBad("subtype", *subtype, "See RFC-11 for possible values"_tr);
            }
        }
    }

    std::string order;
    OrderDir dir = OrderDir::Asc;

    if (orderBy) {
        if (auto find = possibleOrders.find(*orderBy); find == possibleOrders.end()) {
            throw rest::errors::RequestParamBad("order_by", *orderBy, implode(possibleOrders, "/"));
        } else {
            order = *orderBy;
        }
    }

    if (orderDir) {
        std::string temp = *orderDir;
        tolower(temp);
        if (temp != "asc" && temp != "desc") {
            throw rest::errors::RequestParamBad("order", *orderDir, "ASC/DESC");
        }
        dir = temp == "asc" ? OrderDir::Asc : OrderDir::Desc;
    }

    pack::Map<pack::ObjectList<Info>> ret;
    auto&                             val = ret.append(*assetType + "s");

    // Get data
    auto allAssetsShort = AssetManager::getItems(*assetType, subtypes, order, dir);
    if (!allAssetsShort) {
        throw rest::errors::Internal(allAssetsShort.error());
    }

    for (const auto& [id, name] : *allAssetsShort) {
        auto assetNames = db::idToNameExtName(id);
        if (!assetNames) {
            throw rest::errors::Internal("Database failure"_tr);
        }

        auto& ins = val.append();
        ins.id    = id;
        ins.name  = assetNames->second;
    }

    m_reply << *pack::json::serialize(ret);
    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::List)
