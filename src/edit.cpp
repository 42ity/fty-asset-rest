/*  ========================================================================================================================================
    Copyright (C) 2014 - 2020 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    ========================================================================================================================================
*/

#include <cxxtools/log.h> // tntnet13/cxxtools10 : fix missed cxxtools::LogConfiguration ref.

#include "edit.h"
#include <asset/asset-cam.h>
#include <asset/asset-configure-inform.h>
#include <asset/asset-import.h>
#include <asset/asset-manager.h>
#include <asset/asset-notifications.h>
#include <asset/csv.h>
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>
#include <fty_common.h>
//#include <fty_asset_activator.h>
#include <asset/asset-helpers.h>
#include <fty_common_asset.h>
#include <fty_common_mlm.h>

#include <mutex>

namespace fty::asset {

#define AGENT_ASSET_ACTIVATOR "etn-licensing-credits"

static std::mutex g_mutex;

unsigned Edit::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    Expected<std::string> id = m_request.queryArg<std::string>("id");
    if (!id) {
        auditError("Request CREATE OR UPDATE asset FAILED: {}"_tr, "Asset id is not set"_tr);
        throw rest::errors::RequestParamRequired("id");
    }

    if (!persist::is_ok_name(id->c_str())) {
        auditError("Request CREATE OR UPDATE asset FAILED: {}"_tr, "Asset id is not valid"_tr);
        throw rest::errors::RequestParamBad("id", *id, "Valid id"_tr);
    }

    auto before = AssetManager::getDto(*id);

    cxxtools::SerializationInfo si;
    try {
        JSON::readFromString(m_request.body(), si);
        auto        si_id = si.findMember("id");
        std::string status;
        si.getMember("status") >>= status;
        std::string type;
        si.getMember("type") >>= type;

        if (!si_id) {
            si.addMember("id") <<= *id;
        } else if (status == "nonactive") {
            if (*id == "rackcontroller-0" || persist::is_container(type)) {
                logDebug("Element {} cannot be inactivated.", *id);
                auditError("Request CREATE OR UPDATE asset id {} FAILED"_tr, *id);
                throw rest::errors::ActionForbidden("inactivate", "Inactivation of this asset"_tr);
            }
        } else {
            si_id->setValue(*id);
        }
    } catch (const std::exception& e) {
        auditError("Request CREATE OR UPDATE asset id {} FAILED"_tr, *id);
        throw rest::errors::Internal(e.what());
    }

    // IPMVAL-4513 Hotfix: protect concurent db access for drag and drop pdu in rack view
    std::lock_guard<std::mutex> lock(g_mutex);

    CsvMap cm;
    try {
        cm = CsvMap_from_serialization_info(si);
        cm.setUpdateUser(user.login());
        std::time_t timestamp = std::time(nullptr);
        char        timeString[100];
        if (std::strftime(timeString, sizeof(timeString), "%FT%T%z", std::localtime(&timestamp))) {
            cm.setUpdateTs(timeString);
        }
    } catch (const std::invalid_argument& e) {
        logError(e.what());
        auditError("Request CREATE OR UPDATE asset id {} FAILED"_tr, *id);
        throw rest::errors::BadRequestDocument(e.what());
    } catch (const std::exception& e) {
        auditError("Request CREATE OR UPDATE asset id {} FAILED"_tr, *id);
        throw rest::errors::Internal(e.what());
    }

    // PUT /asset is currently used to update an existing device (only asset_element and ext_attributes)
    //      for EMC4J.
    // empty document
    if (cm.cols() == 0 || cm.rows() == 0) {
        auditError("Request CREATE OR UPDATE asset id {} FAILED", *id);
        throw rest::errors::BadRequestDocument("Cannot import empty document."_tr);
    }

    if (!cm.hasTitle("type")) {
        auditError("Request CREATE OR UPDATE asset id {} FAILED", *id);
        throw rest::errors::RequestParamRequired("type"_tr);
    }

    logDebug("starting load");
    Import import(cm);
    if (auto res = import.process(true)) {
        const auto& imported = import.items();
        if (imported.find(1) == imported.end()) {
            throw rest::errors::Internal("Request CREATE OR UPDATE asset id {} FAILED"_tr.format(*id));
        }

        if (imported.at(1)) {
            // this code can be executed in multiple threads -> agent's name should
            // be unique at the every moment
            std::string agent_name = generateMlmClientId("web.asset_put");
            if (auto sent = sendConfigure(*(imported.at(1)), import.operation(), agent_name); !sent) {
                logError(sent.error());
                throw rest::errors::Internal(sent.error());
            }

            // no unexpected errors was detected
            // process results
            auto ret = db::idToNameExtName(imported.at(1)->id);
            if (!ret) {
                logError(ret.error());
                throw rest::errors::Internal(ret.error());
            }
            m_reply << "{\"id\": \"" << ret->first << "\"}";
            auditInfo("Request CREATE OR UPDATE asset id {} SUCCESS"_tr, *id);

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

            if (auto after = AssetManager::getDto(*id); before && after) {

                notification::updated::PayloadFull full;
                full.before                               = *before;
                full.after                                = *after;
                notification::updated::PayloadLight light = after->name;

                // full notification
                if (auto json = pack::json::serialize(full, pack::Option::WithDefaults)) {
                    if (auto send = sendStreamNotification(notification::updated::Topic::Full, notification::updated::Subject::Full, *json);
                        !send) {
                        log_error("Failed to send update notification: %s", send.error().c_str());
                    }
                } else {
                    log_error("Failed to serialize notification payload: %s", json.error().c_str());
                }

                // light notification
                if (auto json = pack::json::serialize(light, pack::Option::WithDefaults)) {
                    if (auto send =
                            sendStreamNotification(notification::updated::Topic::Light, notification::updated::Subject::Light, *json);
                        !send) {
                        log_error("Failed to send update light notification: %s", send.error().c_str());
                    }
                } else {
                    log_error("Failed to serialize notification light payload: %s", json.error().c_str());
                }
            } else {
                if (!before) {
                    log_error("Failed to get asset DTO: %s", before.error().message().c_str());
                }
                if (!after) {
                    log_error("Failed to get asset DTO: %s", after.error().message().c_str());
                }
            }

            try {
                fty::FullAsset asset(si);
                si >>= asset;

                if (asset.getTypeString() == "device") {
                    if (asset.getStatusString() == "active") {
                        if (!activation::isActivable(asset)) {
                            throw std::runtime_error("Asset cannot be activated"_tr);
                        }
                        activation::activate(asset);
                    } else {
                        activation::deactivate(asset);
                    }
                }
            } catch (const std::exception& e) {
                auditError("Request CREATE OR UPDATE asset id {} FAILED"_tr, *id);
                throw rest::errors::LicensingErr(e.what());
            }

            return HTTP_OK;
        } else {
            auditError("Request CREATE OR UPDATE asset id {} FAILED: {}"_tr, *id, imported.at(1).error());
            throw rest::errors::Internal("Import failed: {}"_tr.format(imported.at(1).error()));
        }
    } else {
        throw rest::errors::Internal(res.error());
    }
}

} // namespace fty::asset

registerHandler(fty::asset::Edit)
