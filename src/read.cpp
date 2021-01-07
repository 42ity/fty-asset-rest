/*  ====================================================================================================================
    read.cpp - Implementation of GET operation on any asset

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

#include "read.h"
#include "asset/asset-helpers.h"
#include "asset/json.h"
#include <fty/rest/audit-log.h>
#include <fty/rest/component.h>
#include <fty_common_asset_types.h>

namespace fty::asset {

unsigned Read::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    auto strIdPrt = m_request.queryArg<std::string>("id");
    auto typePtr  = m_request.queryArg<std::string>("type");

    if (!strIdPrt) {
        throw rest::errors::RequestParamRequired("id");
    }

    std::string strId(*strIdPrt);
    std::string type = typePtr ? std::string(*typePtr) : std::string();

    if (!type.empty() && !persist::type_to_typeid(type)) {
        throw rest::errors::RequestParamBad("type", type, "one of datacenter/room/row/rack/group/device"_tr);
    }

    uint32_t id = 0;
    if (auto res = checkElementIdentifier("dev", strId)) {
        id = *res;
    } else if (!type.empty()) {
        if (auto res2 = checkElementIdentifier("dev", type + strId)) {
            id = *res2;
        }
    }
    if (!id) {
        throw rest::errors::ElementNotFound(strId);
    }


    std::string jsonAsset = getJsonAsset(id);

    if (jsonAsset.empty()) {
        throw rest::errors::Internal("get json asset failed."_tr);
    }
    m_reply << jsonAsset << "\n\n";
    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::Read)
