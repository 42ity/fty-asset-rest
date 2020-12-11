/*
 *
 * Copyright (C) 2015 - 2018 Eaton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "asset/asset-cam.h"

#include <algorithm>
#include <fty/split.h>

static constexpr const char* SECW_CRED_ID_KEY = "secw_credential_id";

void operator>>=(const cxxtools::SerializationInfo& si, ExtMapElement& e){
    si.getMember("value") >>= e.value;
    si.getMember("readOnly") >>= e.readOnly;
    si.getMember("update") >>= e.wasUpdated;
}

void operator>>=(const cxxtools::SerializationInfo& si, ExtMap& map) {
    const cxxtools::SerializationInfo ext = si.getMember("ext");
    for (const auto& siExt : ext) {
        std::string   key = siExt.name();
        ExtMapElement element;
        siExt >>= element;
        map[key] = element;
    }
}

std::list<CredentialMapping> getCredentialMappings(const ExtMap& extMap) {
    std::list<CredentialMapping> credentialList;

    auto findCredKey = [&] (const auto& el) {
        return el.first.find(SECW_CRED_ID_KEY) != std::string::npos;
    };

    // lookup for ext attributes which contains secw_credential_id in the key
    auto found = std::find_if(extMap.begin(), extMap.end(), findCredKey);

    // create mapping
    while(found != extMap.end()) {
        CredentialMapping c;
        c.credentialId = found->second.value;

        // extract protocol from element key (endpoint.XX.protocol.secw_credential_id)
        auto keyTokens = fty::split(found->first, ".");
        c.protocol = keyTokens.size() >= 3 ? keyTokens[2] : CAM_DEFAULT_PROTOCOL;
        credentialList.push_back(c);

        found = std::find_if(extMap.begin(), extMap.end(), findCredKey);
    }
    return credentialList;
}
