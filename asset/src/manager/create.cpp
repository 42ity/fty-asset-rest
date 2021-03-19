#include "asset/asset-cam.h"
#include "asset/asset-configure-inform.h"
#include "asset/asset-import.h"
#include "asset/asset-manager.h"
#include "asset/csv.h"
#include "asset/logger.h"
#include <fty_asset_activator.h>
#include <mutex>

namespace fty::asset {

#define AGENT_ASSET_ACTIVATOR "etn-licensing-credits"
#define CREATE_MODE_ONE_ASSET 1
#define CREATE_MODE_CSV       2

// ensure only 1 request is process at the time
static std::mutex g_modification;

AssetExpected<uint32_t> AssetManager::createAsset(const std::string& json, const std::string& user, bool sendNotify)
{
    auto msg = "Request CREATE asset {} FAILED: {}"_tr;
    // Read json, transform to csv, use existing functionality
    cxxtools::SerializationInfo si;
    try {
        std::stringstream          input(json, std::ios_base::in);
        cxxtools::JsonDeserializer deserializer(input);
        deserializer.deserialize(si);
    } catch (const std::exception& e) {
        logError(e.what());
        return unexpected(msg.format("", e.what()));
    }

    std::string itemName;
    si.getMember("name", itemName);

    logDebug("Create asset {}", itemName);

    const std::lock_guard<std::mutex> lock(g_modification);

    CsvMap cm;
    try {
        cm = CsvMap_from_serialization_info(si);
        cm.setCreateUser(user);
        cm.setCreateMode(CREATE_MODE_ONE_ASSET);
    } catch (const std::invalid_argument& e) {
        logError(e.what());
        return unexpected(msg.format(itemName, e.what()));
    } catch (const std::exception& e) {
        logError(e.what());
        return unexpected(msg.format(itemName, e.what()));
    }

    if (cm.cols() == 0 || cm.rows() == 0) {
        logError("Request CREATE asset {} FAILED, import is empty", itemName);
        return unexpected(msg.format(itemName, "Cannot import empty document."_tr));
    }

    // in underlying functions like update_device
    if (!cm.hasTitle("type")) {
        logError("Type is not set");
        return unexpected(msg.format(itemName, "Asset type is not set"));
    }
    if (cm.hasTitle("id")) {
        logError("key 'id' is forbidden to be used");
        return unexpected(msg.format(itemName, "key 'id' is forbidden to be used"_tr));
    }

    if (sendNotify) {
        fty::FullAsset asset;
        try {
            si >>= asset;
        } catch (const std::invalid_argument& e) {
            logError(e.what());
            return unexpected(msg.format(itemName, e.what()));
        } catch (const std::exception& e) {
            logError(e.what());
            return unexpected(msg.format(itemName, e.what()));
        }

        try {
            if (asset.isPowerAsset() && asset.getStatusString() == "active") {
                mlm::MlmSyncClient  client(AGENT_FTY_ASSET, AGENT_ASSET_ACTIVATOR);
                fty::AssetActivator activationAccessor(client);
                if (!activationAccessor.isActivable(asset)) {
                    return unexpected(msg.format(itemName, "Asset cannot be activated"_tr));
                }
                activationAccessor.activate(asset);
            }
        } catch (const std::exception& e) {
            logError(e.what());
            return unexpected(msg.format(itemName, e.what()));
        }
    }

    // actual insert - throws exceptions
    Import import(cm);
    if (auto res = import.process(sendNotify)) {
        // here we are -> insert was successful
        // ATTENTION:  1. sending messages is "hidden functionality" from user
        //             2. if any error would occur during the sending message,
        //                user will never know if element was actually inserted
        //                or not
        const auto& imported = import.items();
        if (imported.find(1) == imported.end()) {
            return unexpected(msg.format(itemName, "Import failed"_tr));
        }

        if (imported.at(1)) {
            if (sendNotify) {
                // this code can be executed in multiple threads -> agent's name should
                // be unique at the every moment
                std::string agent_name = generateMlmClientId("web.asset_post");
                if (auto sent = sendConfigure(*(imported.at(1)), import.operation(), agent_name); !sent) {
                    logError(sent.error());
                    return unexpected(
                        "Error during configuration sending of asset change notification. Consult system log."_tr);
                }

                // no unexpected errors was detected
                // process results
                auto ret = db::idToNameExtName(imported.at(1)->id);
                if (!ret) {
                    logError(ret.error());
                    return unexpected(msg.format(itemName, "Database failure"_tr));
                }
                try {
                    ExtMap map;
                    getExtMapFromSi(si, map);

                    const auto& assetIname = ret.value().first;

                    deleteMappings(assetIname);
                    auto credentialList = getCredentialMappings(map);
                    createMappings(assetIname, credentialList);
                } catch (const std::exception& e) {
                    log_error("Failed to update CAM: %s", e.what());
                }
            }
            return imported.at(1)->id;
        } else {
            return unexpected(msg.format(itemName, imported.at(1).error()));
        }
    } else {
        return unexpected(msg.format(itemName, res.error()));
    }
}

} // namespace fty::asset
