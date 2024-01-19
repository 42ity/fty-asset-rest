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

#include "export.h"
#include <asset/asset-db.h>
#include <asset/asset-manager.h>
#include <chrono>
#include <fty/rest/component.h>
#include <fty_common_asset_types.h>
#include <regex>
#include <iomanip>

namespace fty::asset {

unsigned Export::run()
{
    rest::User user(m_request);
    if (auto ret = checkPermissions(user.profile(), m_permissions); !ret) {
        throw rest::Error(ret.error());
    }

    auto                            dc = m_request.queryArg<std::string>("dc");
    std::optional<db::AssetElement> dcAsset = std::nullopt;
    if (dc) {
        auto asset = db::selectAssetElementByName(*dc);
        if (!asset || asset->typeId != persist::type_to_typeid("datacenter")) {
            throw rest::errors::RequestParamBad("dc", "not a datacenter"_tr, "existing asset which is a datacenter"_tr);
        }
        dcAsset = *asset;
    }

    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%FT%TZ");
    std::string strTime = std::regex_replace(ss.str(), std::regex(":"), "-");

    if (dcAsset != std::nullopt) {
        auto dcENameRet = db::idToNameExtName(dcAsset->id);
        if (!dcENameRet) {
            throw rest::errors::ElementNotFound(dcAsset->id);
        }
        // escape special characters
        std::string dcEName = std::regex_replace(dcENameRet->second, std::regex("( |\t)"), "_");
        m_reply.setHeader(tnt::httpheader::contentDisposition,
            "attachment; filename=\"asset_export_" + dcEName + "_" + strTime + ".csv\"");
    } else {
        m_reply.setHeader(
            tnt::httpheader::contentDisposition, "attachment; filename=\"asset_export_" + strTime + ".csv\"");
    }

    auto ret = AssetManager::exportCsv(dcAsset);
    if (ret) {
        m_reply.setContentType("text/csv;charset=UTF-8");
        m_reply << "\xef\xbb\xbf";
        m_reply << *ret;
    } else {
        throw rest::errors::Internal(ret.error());
    }

    return HTTP_OK;
}

} // namespace fty::asset

registerHandler(fty::asset::Export)
