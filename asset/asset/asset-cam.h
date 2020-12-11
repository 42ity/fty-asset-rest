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

#pragma once
#include <cxxtools/serializationinfo.h>
#include <list>
#include <map>
#include <string>

// cam/secw interface constants
static constexpr const char *MALAMUTE_ENDPOINT = "ipc://@/malamute";
static constexpr const char *CAM_CLIENT_ID = "asset-agent";
static constexpr const char *CAM_SERVICE_ID = "monitoring";
static constexpr const char *CAM_DEFAULT_PROTOCOL = "nut_snmp";
static constexpr const int   CAM_TIMEOUT_MS = 1000; // ms

struct CredentialMapping {
    std::string credentialId;
    std::string protocol;
};

struct ExtMapElement {
    std::string value;
    bool        readOnly   = false;
    bool        wasUpdated = false;
};

using ExtMap = std::map<std::string, ExtMapElement>;

void operator>>=(const cxxtools::SerializationInfo& si, ExtMapElement& e);
void operator>>=(const cxxtools::SerializationInfo& si, ExtMap& map);

std::list<CredentialMapping> getCredentialMappings(const ExtMap& extMap);
