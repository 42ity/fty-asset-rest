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
#include <asset/asset-manager.h>
#include <cxxtools/jsondeserializer.h>
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>

namespace fty::asset {

unsigned Create::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    cxxtools::SerializationInfo si;
    try {
        std::stringstream          jsonIn(m_request.body());
        cxxtools::JsonDeserializer deserializer(jsonIn);
        deserializer.deserialize(si);
    } catch (const std::exception& e) {
        auditError(e.what());
        throw rest::errors::Internal(e.what());
    }

    bool             someOk = false;
    pack::StringList createdName;
    auto             validate = [&](const uint32_t& id) {
        auto createdAsset = fty::asset::db::idToNameExtName(id);

        if (!createdAsset) {
            auditError(createdAsset.error());
            return;
        }

        createdName.append(createdAsset->first.c_str());
        auditInfo("Request CREATE asset id {} SUCCESS"_tr, createdAsset->first.c_str());
        someOk = true;
    };

    cxxtools::SerializationInfo assetsJsonList;
    if (si.findMember("assets")) {
        assetsJsonList = si.getMember("assets");

        for (const auto& it : assetsJsonList) {
            auto ret = AssetManager::createAsset(it, user.login());
            if (!ret) {
                auditError(ret.error());
                continue;
            }
            validate(*ret);
        }
    } else {
        auto ret = AssetManager::createAsset(si, user.login());
        if (!ret) {
            auditError(ret.error());
        } else {
            validate(*ret);
        }
    }

    if (!someOk) {
        throw rest::errors::Internal("Some of assets creation FAILED");
    }

    m_reply << *pack::json::serialize(createdName) << "\n\n";

    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::Create)
