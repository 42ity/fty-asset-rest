/*  ====================================================================================================================
    create.cpp - Implementation of POST (create) operation on any asset

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
    ====================================================================================================================
*/

#include "create.h"
#include <asset/asset-computed.h>
#include <asset/asset-manager.h>
#include <asset/json.h>
#include <cxxtools/jsondeserializer.h>
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>
#include <fty/string-utils.h>
#include <fty_common.h>
#include <fty_common_db_asset.h>
#include <fty_common_rest.h>
#include <fty_shm.h>
#include <vector>

namespace fty::asset {

unsigned Create::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    cxxtools::SerializationInfo si;
    std::stringstream           jsonIn;
    try {
        jsonIn << m_request.body();
        cxxtools::JsonDeserializer deserializer(jsonIn);
        deserializer.deserialize(si);
    } catch (const std::exception& e) {
        logError(e.what());
        return HTTP_CONFLICT;
    }

    bool             someOk = false;
    pack::StringList createdName;
    if (si.findMember("assets")) {
        auto assetsJsonList = si.getMember("assets");
        for (auto it = assetsJsonList.begin(); it != assetsJsonList.end(); ++it) {

            auto ret = AssetManager::createAsset(*it, user.login());
            if (!ret) {
                auditError(ret.error());
                continue;
            }

            auto createdAsset = AssetManager::getItem(*ret);

            if (!createdAsset) {
                auditError(createdAsset.error());
                continue;
            }

            createdName.append(createdAsset->name);
            auditInfo("Request CREATE asset id {} SUCCESS"_tr, createdAsset->name);
            someOk = true;
        }
    } else {
        auto ret = AssetManager::createAsset(jsonIn.str(), user.login());
        if (!ret) {
            auditError(ret.error());
            return HTTP_CONFLICT;
        }

        auto createdAsset = AssetManager::getItem(*ret);

        if (!createdAsset) {
            auditError(createdAsset.error());
        }

        createdName.append(createdAsset->name);
        auditInfo("Request CREATE asset id {} SUCCESS"_tr, createdAsset->name);
        someOk = true;
    }

    m_reply << *pack::json::serialize(createdName) << "\n\n";

    return someOk ? HTTP_OK : HTTP_CONFLICT;
}

} // namespace fty::asset

registerHandler(fty::asset::Create)
