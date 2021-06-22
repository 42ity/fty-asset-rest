#include "delete.h"
#include <asset/asset-configure-inform.h>
#include <asset/asset-db.h>
#include <asset/asset-manager.h>
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>
#include <fty/rest/translate.h>
#include <fty/string-utils.h>
#include <fty_common_asset_types.h>


namespace fty::asset {

unsigned Delete::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    // sanity check
    Expected<std::string> id  = m_request.queryArg<std::string>("id");
    Expected<std::string> ids = m_request.queryArg<std::string>("ids");
    if (!id && !ids) {
        auditError("Request DELETE asset FAILED"_tr);
        throw rest::errors::RequestParamRequired("id");
    }

    if (id) {
        return deleteOneAsset(*id);
    }
    return deleteAssets(*ids);
}

unsigned Delete::deleteOneAsset(const std::string& idStr)
{
    if (!persist::is_ok_name(idStr.c_str())) {
        auditError("Request DELETE asset id {} FAILED"_tr, idStr);
        throw rest::errors::RequestParamBad("id", idStr, "valid asset name"_tr);
    }

    Expected<uint32_t> dbid = db::nameToAssetId(idStr);
    if (!dbid) {
        auditError("Request DELETE asset id {} FAILED: {}"_tr, idStr, dbid.error());
        throw rest::errors::DbErr(dbid.error());
    }

    auto res = AssetManager::deleteAsset(*dbid);
    if (!res) {
        logError(res.error());
        std::string reason = "Asset is in use, remove children/power source links first."_tr;
        auditError("Request DELETE asset id {} FAILED"_tr, idStr);
        throw rest::errors::DataConflict(idStr, reason);
    }

    std::string agent_name = generateMlmClientId("web.asset_delete");
    if (auto ret = sendConfigure(*res, persist::asset_operation::DELETE, agent_name)) {
        m_reply << "{}";
        auditInfo("Request DELETE asset id {} SUCCESS", idStr);
        return HTTP_OK;
    } else {
        logError(ret.error());
        auditError("Request DELETE asset id {} FAILED"_tr, idStr);
        throw rest::errors::Internal(
            "Error during configuration sending of asset change notification. Consult system log."_tr);
    }
}

struct Ret : public pack::Node
{
    pack::String                    asset  = FIELD("asset");
    pack::String                    status = FIELD("status");
    rest::details::TranslateMessage reason = FIELD("reason");

    using pack::Node::Node;
    META(Ret, asset, status, reason);
};

unsigned Delete::deleteAssets(const std::string& idsStr)
{
    std::vector<std::string> ids = fty::split(idsStr, ",");

    for (const auto& id : ids) {
        if (!persist::is_ok_name(id.c_str())) {
            auditError("Request DELETE asset id {} FAILED", id);
            throw rest::errors::RequestParamBad("id", id, "valid asset name"_tr);
        }
    }

    std::map<uint32_t, std::string> dbIds;
    for (const auto& id : ids) {
        if (auto dbid = db::nameToAssetId(id)) {
            dbIds.emplace(*dbid, id);
        } else {
            logError(dbid.error());
            auditError("Request DELETE asset id {} FAILED", id);
            throw rest::errors::RequestParamBad("ids", idsStr, "valid asset name"_tr);
        }
    }

    auto result = AssetManager::deleteAsset(dbIds);

    bool someAreOk = false;
    for (const auto& [name, asset] : result) {
        if (asset) {
            someAreOk = true;
            auditInfo("Request DELETE asset id {} SUCCESS", asset->id);
        } else {
            auditError("Request DELETE asset id {} FAILED with error: {}", name, asset.error());
        }
    }

    pack::ObjectList<Ret> ret;
    std::string           agent_name = generateMlmClientId("web.asset_delete");

    for (const auto& [name, asset] : result) {
        auto& retVal  = ret.append();
        retVal.status = asset ? "OK" : "ERROR";
        retVal.asset  = name;
        if (asset) {
            sendConfigure(*asset, persist::asset_operation::DELETE, agent_name);
        } else {
            rest::json(asset.error(), retVal.reason);
        }
    }

    m_reply << *pack::json::serialize(ret);

    if (!someAreOk) {
        return HTTP_CONFLICT;
    }

    auditInfo("Request DELETE assets ids {} SUCCESS", idsStr);
    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::Delete)
