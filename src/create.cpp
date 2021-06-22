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
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>
#include "asset/asset-manager.h"
#include "asset/json.h"
#include "asset/asset-computed.h"
#include <fty/string-utils.h>
#include <fty_common.h>
#include <fty_common_db_asset.h>
#include <fty_common_rest.h>
#include <fty_shm.h>

namespace fty::asset {

unsigned Create::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    std::string assetJson = m_request.body();

    auto ret = AssetManager::createAsset(assetJson, user.login());
    if (!ret) {
        auditError(ret.error());
        throw rest::errors::Internal(ret.error());
    }

    auto createdAsset = AssetManager::getItem(*ret);

    if (!createdAsset) {
        auditError(createdAsset.error());
        throw rest::errors::Internal(createdAsset.error());        
    } 

    m_reply << "{ " << utils::json::jsonify("id", createdAsset->name) << " }\n\n";

    auditInfo("Request CREATE asset id {} SUCCESS"_tr, createdAsset->name);


    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::Create)
