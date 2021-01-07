#include "asset/asset-cam.h"
#include "asset/asset-db.h"
#include "asset/asset-manager.h"
#include "asset/db.h"
#include "asset/json.h"
#include "asset/logger.h"
#include <fty_asset_activator.h>
#include <fty_common_asset_types.h>

namespace fty::asset {

static constexpr const char* ENV_OVERRIDE_LAST_DC_DELETION_CHECK = "FTY_OVERRIDE_LAST_DC_DELETION_CHECK";
static constexpr const char* AGENT_ASSET_ACTIVATOR               = "etn-licensing-credits";

// =====================================================================================================================

struct Element;
using ElementPtr = std::shared_ptr<Element>;
struct Element : public db::WebAssetElement
{
    std::vector<ElementPtr> chidren;
    std::vector<ElementPtr> links;
    bool                    isDeleted = false;
};

// =====================================================================================================================

// Collect children, filter by ids which should be deleted. If some ids will be in children, then we cannot delete asset
static void collectChildren(
    uint32_t parentId, std::vector<uint32_t>& children, const std::map<uint32_t, std::string>& ids)
{
    if (auto ret = db::selectAssetsByParent(parentId)) {
        for (uint32_t id : *ret) {
            if (ids.find(id) == ids.end()) {
                children.push_back(id);
            }
            collectChildren(id, children, ids);
        }
    }
}


// Collect links, filter by ids which should be deleted. If some ids will be in links, then we cannot delete asset
static void collectLinks(uint32_t elementId, std::vector<uint32_t>& links, const std::map<uint32_t, std::string>& ids)
{
    if (auto ret = db::selectAssetDeviceLinksSrc(elementId)) {
        for (uint32_t id : *ret) {
            if (ids.find(id) == ids.end()) {
                links.push_back(id);
            }
        }
    }
}

// Delete asset recursively
static AssetExpected<void> deleteAssetRec(ElementPtr& el, bool sendNotify)
{
    if (el->isDeleted) {
        return {};
    }

    for (auto& it : el->links) {
        if (auto ret = deleteAssetRec(it, sendNotify); !ret) {
            return unexpected(ret.error());
        }
    }
    for (auto& it : el->chidren) {
        if (auto ret = deleteAssetRec(it, sendNotify); !ret) {
            return unexpected(ret.error());
        }
    }
    if (auto ret = AssetManager::deleteAsset(*el, sendNotify); !ret) {
        return unexpected(ret.error());
    }
    el->isDeleted = true;
    return {};
}

// Check if datacenter in data centric mode (fast_track == true) and if yes, try to check if this feed is last in dc.
static bool checkFeed(const db::AssetElement& asset)
{
    auto ret = db::findParentByType(asset.id, persist::DATACENTER);
    if (!ret) {
        return true;
    }

    auto attrs = db::selectExtAttributes(ret->id);
    if (!attrs) {
        return true;
    }

    if (attrs->find("fast_track") == attrs->end()) {
        return true;
    }

    if ((*attrs)["fast_track"].value != "true") {
        return true;
    }

    tnt::Connection conn;
    std::vector<uint32_t> ids;
    db::selectAssetsByContainer(conn, ret->id, {persist::DEVICE}, {persist::FEED}, {}, {}, [&](const tnt::Row& row) {
        ids.push_back(row.get<uint32_t>("asset_id"));
    });

    if (ids.size() == 1 && ids[0] == asset.id) {
        return false;
    }

    return true;
}

// =====================================================================================================================

AssetExpected<db::AssetElement> AssetManager::deleteAsset(uint32_t id, bool sendNotify)
{
    auto asset = db::selectAssetElementWebById(id);
    if (!asset) {
        return unexpected(asset.error());
    }
    return deleteAsset(*asset, sendNotify);
}

AssetExpected<db::AssetElement> AssetManager::deleteAsset(const db::AssetElement& asset, bool sendNotify)
{
    // disable deleting RC0
    if (asset.name == "rackcontroller-0") {
        logDebug("Prevented deleting RC-0");
        return unexpected("Prevented deleting RC-0");
    }

    if (asset.subtypeId == persist::FEED) {
        if (!checkFeed(asset)) {
            return unexpected("Last feed cannot be deleted in device centric mode"_tr);
        }
    }

    // check if a logical_asset refer to the item we are trying to delete
    if (auto res = db::countKeytag("logical_asset", asset.name); res && *res > 0) {
        logWarn("a logical_asset (sensor) refers to it");
        return unexpected("a logical_asset (sensor) refers to it"_tr);
    }

    // make the device inactive first
    if (asset.status == "active" && sendNotify) {
        std::string asset_json = getJsonAsset(asset.id);

        try {
            mlm::MlmSyncClient  client(AGENT_FTY_ASSET, AGENT_ASSET_ACTIVATOR);
            fty::AssetActivator activationAccessor(client);
            activationAccessor.deactivate(asset_json);
        } catch (const std::exception& e) {
            logError("Error during asset deactivation - {}", e.what());
            return unexpected(e.what());
        }
    }

    auto ret = [&]() -> AssetExpected<db::AssetElement> {
        switch (asset.typeId) {
            case persist::asset_type::DATACENTER:
            case persist::asset_type::ROW:
            case persist::asset_type::ROOM:
            case persist::asset_type::RACK:
                return deleteDcRoomRowRack(asset);
            case persist::asset_type::GROUP:
                return deleteGroup(asset);
            case persist::asset_type::DEVICE: {
                return deleteDevice(asset);
            }
        }

        logError("unknown type");
        return unexpected("unknown type"_tr);
    }();

    // in case of error we need to try to activate the asset again.
    if (!ret && asset.status == "active" && sendNotify) {
        std::string asset_json = getJsonAsset(asset.id);

        try {
            mlm::MlmSyncClient  client(AGENT_FTY_ASSET, AGENT_ASSET_ACTIVATOR);
            fty::AssetActivator activationAccessor(client);
            activationAccessor.activate(asset_json);
        } catch (const std::exception& e) {
            logError("Error during asset activation - {}", e.what());
        }
    }

    if (sendNotify) {
        try {
            logDebug("Deleting all mappings for asset {}", asset.name);
            deleteMappings(asset.name);
        } catch (const std::exception& e) {
            log_error("Failed to update CAM: %s", e.what());
        }
    }

    return ret ? AssetExpected<db::AssetElement>(*ret) : unexpected(ret.error());
}

std::map<std::string, AssetExpected<db::AssetElement>> AssetManager::deleteAsset(
    const std::map<uint32_t, std::string>& ids, bool sendNotify)
{
    std::map<std::string, AssetExpected<db::AssetElement>> result;
    std::vector<std::shared_ptr<Element>>                  toDel;

    for (const auto& [id, name] : ids) {
        ElementPtr el = std::make_shared<Element>();

        if (auto ret = db::selectAssetElementWebById(id, *el)) {
            std::vector<uint32_t> allChildren;
            collectChildren(el->id, allChildren, ids);

            std::vector<uint32_t> links;
            collectLinks(el->id, links, ids);

            if (!allChildren.empty() || !links.empty()) {
                result.emplace(
                    name, unexpected("can't delete the asset because it has at least one child or asset is linked"_tr));
            } else {
                toDel.push_back(el);
            }
        } else {
            result.emplace(name, unexpected(ret.error()));
        }
    }

    auto find = [&](uint32_t id) {
        auto ret = std::find_if(toDel.begin(), toDel.end(), [&](const auto& ptr) {
            return ptr->id == id;
        });
        return ret != toDel.end() ? *ret : nullptr;
    };

    for (auto& it : toDel) {
        // Collect element children
        if (auto ret = db::selectAssetsByParent(it->id)) {
            for (uint32_t id : *ret) {
                if (auto el = find(id)) {
                    it->chidren.push_back(el);
                }
            }
        }
        // Collect element links
        if (auto ret = db::selectAssetDeviceLinksSrc(it->id)) {
            for (uint32_t id : *ret) {
                if (auto el = find(id)) {
                    it->links.push_back(el);
                }
            }
        }
    }

    // Delete all elements recursively
    for (auto& it : toDel) {
        if (auto ret = deleteAssetRec(it, sendNotify)) {
            result.emplace(it->name, *it);
        } else {
            result.emplace(it->name, unexpected(ret.error()));
        }
    }

    return result;
}


AssetExpected<db::AssetElement> AssetManager::deleteDcRoomRowRack(const db::AssetElement& element)
{
    tnt::Connection  conn;
    tnt::Transaction trans(conn);

    static const std::string countSql = R"(
        SELECT
            COUNT(id_asset_element) as count
        FROM
            t_bios_asset_element
        WHERE
            id_type = (select id_asset_element_type from  t_bios_asset_element_type where name = "datacenter") AND
            id_asset_element != :element
    )";

    // Don't allow the deletion of the last datacenter (unless overriden)
    if (getenv(ENV_OVERRIDE_LAST_DC_DELETION_CHECK) == nullptr) {
        unsigned numDatacentersAfterDelete;
        try {
            numDatacentersAfterDelete = conn.selectRow(countSql, "element"_p = element.id).get<unsigned>("count");
        } catch (const std::exception& e) {
            return unexpected(e.what());
        }
        if (numDatacentersAfterDelete == 0) {
            return unexpected("will not allow last datacenter to be deleted"_tr);
        }
    }

    if (auto ret = db::deleteAssetElementFromAssetGroups(conn, element.id); !ret) {
        trans.rollback();
        logInfo("error occured during removing from groups");
        return unexpected("error occured during removing from groups"_tr);
    }

    if (auto ret = db::convertAssetToMonitor(element.id); !ret) {
        logError(ret.error());
        return unexpected("error during converting asset to monitor"_tr);
    }

    if (auto ret = db::deleteMonitorAssetRelationByA(conn, element.id); !ret) {
        trans.rollback();
        logError(ret.error());
        return unexpected("error occured during removing ma relation"_tr);
    }

    if (auto ret = db::deleteAssetElement(conn, element.id); !ret) {
        trans.rollback();
        logError(ret.error());
        return unexpected("error occured during removing element"_tr);
    }

    trans.commit();
    return element;
}

AssetExpected<db::AssetElement> AssetManager::deleteGroup(const db::AssetElement& element)
{
    tnt::Connection  conn;
    tnt::Transaction trans(conn);

    if (auto ret = db::deleteAssetGroupLinks(conn, element.id); !ret) {
        trans.rollback();
        logError(ret.error());
        return unexpected("error occured during removing from groups"_tr);
    }

    if (auto ret = db::deleteAssetElement(conn, element.id); !ret) {
        trans.rollback();
        logError(ret.error());
        return unexpected("error occured during removing element"_tr);
    }

    trans.commit();
    return element;
}

AssetExpected<db::AssetElement> AssetManager::deleteDevice(const db::AssetElement& element)
{
    tnt::Connection  conn;
    tnt::Transaction trans(conn);

    if (auto ret = db::deleteAssetElementFromAssetGroups(conn, element.id); !ret) {
        trans.rollback();
        logError(ret.error());
        return unexpected("error occured during removing from groups"_tr);
    }

    if (auto ret = db::deleteAssetLinksTo(conn, element.id); !ret) {
        trans.rollback();
        logError(ret.error());
        return unexpected("error occured during removing links"_tr);
    }

    if (auto ret = db::deleteMonitorAssetRelationByA(conn, element.id); !ret) {
        trans.rollback();
        logError(ret.error());
        return unexpected("error occured during removing ma relation"_tr);
    }

    if (auto ret = db::deleteAssetElement(conn, element.id); !ret) {
        trans.rollback();
        logError(ret.error());
        return unexpected("error occured during removing element"_tr);
    }

    trans.commit();
    return element;
}

} // namespace fty::asset
