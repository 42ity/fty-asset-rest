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

#include "asset/json.h"
#include "asset/asset-computed.h"
#include "asset/asset-manager.h"
#include <fty/split.h>
#include <fty_common.h>
#include <fty_common_db_asset.h>
#include <fty_common_rest.h>
#include <fty_shm.h>

namespace fty::asset {

struct Outlet
{
    std::string label;
    bool        label_r;
    std::string type;
    bool        type_r;
    std::string group;
    bool        group_r;
};

static std::string getOutletNumber(const std::string& extAttributeName)
{
    auto        dot1    = extAttributeName.find_first_of(".");
    std::string oNumber = extAttributeName.substr(dot1 + 1);
    auto        dot2    = oNumber.find_first_of(".");
    oNumber             = oNumber.substr(0, dot2);
    return oNumber;
}

static double s_rack_realpower_nominal(const std::string& name)
{
    double      ret = 0.0;
    std::string value;
    if (fty::shm::read_metric_value(name, "realpower.nominal", value) != 0) {
        log_warning("No realpower.nominal for '%s'", name.c_str());
    } else {
        try {
            ret = std::stod(value);
        } catch (const std::exception&) {
            log_error(
                "the metric returned a string that does not encode a double value: '%s'. Defaulting to 0.0 value.",
                value.c_str());
            ret = std::nan("");
        }
    }

    return ret;
}

std::string getJsonAsset(uint32_t elemId)
{
    std::string json;
    // Get informations from database
    auto tmp = AssetManager::getItem(elemId);

    if (!tmp) {
        log_error(tmp.error().toString().c_str());
        return json;
    }

    std::string parent_name;
    std::string ext_parent_name;
    auto parent_names = db::idToNameExtName(tmp->parentId);
    if (parent_names) {
        parent_name = parent_names->first;
        ext_parent_name = parent_names->second;
    }

    std::pair<std::string, std::string> asset_names = DBAssets::id_to_name_ext_name(tmp->id);
    if (asset_names.first.empty() && asset_names.second.empty()) {
        log_error("Database failure");
        return json;
    }
    std::string asset_ext_name = asset_names.second;

    json += "{";

    json += utils::json::jsonify("id", tmp->name) + ",";
    json += "\"power_devices_in_uri\": \"/api/v1/assets\?in=";
    json += (tmp->name);
    json += "&sub_type=epdu,pdu,feed,genset,ups,sts,rackcontroller\",";
    json += utils::json::jsonify("name", asset_ext_name) + ",";
    json += utils::json::jsonify("status", tmp->status) + ",";
    json += utils::json::jsonify("priority", "P" + std::to_string(tmp->priority)) + ",";
    json += utils::json::jsonify("type", tmp->typeName) + ",";

    // if element is located, then show the location
    if (tmp->parentId != 0) {
        json += utils::json::jsonify("location_uri", "/api/v1/asset/" + parent_name) + ",";
        json += utils::json::jsonify("location_id", parent_name) + ",";
        json += utils::json::jsonify("location", ext_parent_name) + ",";

        // Get informations from database
        auto parentAsset = AssetManager::getItem(tmp->parentId);
        json += utils::json::jsonify("location_type", trimmed(parentAsset->typeName)) + ",";
    } else {
        json += "\"location\":\"\",";
        json += "\"location_type\":\"\",";
    }

    json += "\"groups\": [";
    // every element (except groups) can be placed in some group
    if (!tmp->groups.empty()) {
        size_t      group_count = tmp->groups.size();
        uint32_t    i           = 1;
        std::string ext_name    = "";
        for (auto& oneGroup : tmp->groups) {
            std::pair<std::string, std::string> group_names = DBAssets::id_to_name_ext_name(oneGroup.first);
            if (group_names.first.empty() && group_names.second.empty()) {
                log_error("Database failure");
                json = "";
                return json;
            }
            ext_name = group_names.second;
            json += "{";
            json += utils::json::jsonify("id", oneGroup.second) + ",";
            json += utils::json::jsonify("name", ext_name);
            json += "}";

            if (i != group_count) {
                json += ",";
            }
            i++;
        }
    }
    json += "]";

    // Device is special element with more attributes
    if (tmp->typeId == persist::asset_type::DEVICE) {
        json += ", \"powers\": [";

        if (!tmp->powers.empty()) {
            size_t   power_count = tmp->powers.size();
            uint32_t i           = 1;
            for (auto& oneLink : tmp->powers) {
                auto link_names = db::idToNameExtName(oneLink.srcId);
                if (!link_names || (link_names->first.empty() && link_names->second.empty())) {
                    log_error("Database failure");
                    json = "";
                    return json;
                }
                json += "{";
                json += utils::json::jsonify("src_name", link_names->second) + ",";
                json += utils::json::jsonify("src_id", oneLink.srcName);

                if (!oneLink.srcSocket.empty()) {
                    json += "," + utils::json::jsonify("src_socket", oneLink.srcSocket);
                }
                if (!oneLink.destSocket.empty()) {
                    json += "," + utils::json::jsonify("dest_socket", oneLink.destSocket);
                }

                json += "}";
                if (i != power_count) {
                    json += ",";
                }
                i++;
            }
        }

        json += "]";
    }
    // ACE: to be consistent with RFC-11 this was put here
    if (tmp->typeId == persist::asset_type::GROUP) {
        auto it = tmp->extAttributes.find("type");
        if (it != tmp->extAttributes.end()) {
            json += "," + utils::json::jsonify("sub_type", trimmed(it->second.value));
            tmp->extAttributes.erase(it);
        }
    } else {
        json += "," + utils::json::jsonify("sub_type", trimmed(tmp->subtypeName));

        json += ", \"parents\" : [";
        size_t i = 1;

        std::string ext_name = "";

        for (const auto& it : tmp->parents) {
            char                                comma    = i != tmp->parents.size() ? ',' : ' ';
            std::pair<std::string, std::string> it_names = DBAssets::id_to_name_ext_name(std::get<0>(it));
            if (it_names.first.empty() && it_names.second.empty()) {
                log_error("Database failure");
                json = "";
                return json;
            }
            ext_name = it_names.second;
            json += "{";
            json += utils::json::jsonify("id", std::get<1>(it));
            json += "," + utils::json::jsonify("name", ext_name);
            json += "," + utils::json::jsonify("type", std::get<2>(it));
            json += "," + utils::json::jsonify("sub_type", std::get<3>(it));
            json += "}";
            json += (comma);
            i++;
        }
        json += "]";
    }

    json += ", \"ext\" : [";
    bool isExtCommaNeeded = false;

    if (!tmp->assetTag.empty()) {
        json += "{";
        json += utils::json::jsonify("asset_tag", tmp->assetTag);
        json += ", \"read_only\" : false}";
        isExtCommaNeeded = true;
    }

    std::map<std::string, Outlet> outlets;
    std::vector<std::string>      ips;
    std::vector<std::string>      macs;
    std::vector<std::string>      fqdns;
    std::vector<std::string>      hostnames;
    if (!tmp->extAttributes.empty()) {
        cxxtools::Regex r_outlet_label("^outlet\\.[0-9][0-9]*\\.label$");
        cxxtools::Regex r_outlet_group("^outlet\\.[0-9][0-9]*\\.group$");
        cxxtools::Regex r_outlet_type("^outlet\\.[0-9][0-9]*\\.type$");
        cxxtools::Regex r_ip("^ip\\.[0-9][0-9]*$");
        cxxtools::Regex r_mac("^mac\\.[0-9][0-9]*$");
        cxxtools::Regex r_hostname("^hostname\\.[0-9][0-9]*$");
        cxxtools::Regex r_fqdn("^fqdn\\.[0-9][0-9]*$");
        for (auto& oneExt : tmp->extAttributes) {
            auto& attrName = oneExt.first;

            if (attrName == "name")
                continue;

            // filter location_type (already present)
            if (attrName == "location_type")
                continue;

            auto& attrValue  = oneExt.second.value;
            auto  isReadOnly = oneExt.second.readOnly;
            if (r_outlet_label.match(attrName)) {
                auto oNumber = getOutletNumber(attrName);
                auto it      = outlets.find(oNumber);
                if (it == outlets.cend()) {
                    auto r = outlets.emplace(oNumber, Outlet());
                    it     = r.first;
                }
                it->second.label   = attrValue;
                it->second.label_r = isReadOnly;
                continue;
            } else if (r_outlet_group.match(attrName)) {
                auto oNumber = getOutletNumber(attrName);
                auto it      = outlets.find(oNumber);
                if (it == outlets.cend()) {
                    auto r = outlets.emplace(oNumber, Outlet());
                    it     = r.first;
                }
                it->second.group   = attrValue;
                it->second.group_r = isReadOnly;
                continue;
            } else if (r_outlet_type.match(attrName)) {
                auto oNumber = getOutletNumber(attrName);
                auto it      = outlets.find(oNumber);
                if (it == outlets.cend()) {
                    auto r = outlets.emplace(oNumber, Outlet());
                    it     = r.first;
                }
                it->second.type   = attrValue;
                it->second.type_r = isReadOnly;
                continue;
            } else if (r_ip.match(attrName)) {
                ips.push_back(attrValue);
                continue;
            } else if (r_mac.match(attrName)) {
                macs.push_back(attrValue);
                continue;
            } else if (r_fqdn.match(attrName)) {
                fqdns.push_back(attrValue);
                continue;
            } else if (r_hostname.match(attrName)) {
                hostnames.push_back(attrValue);
                continue;
            }
            // If we are here -> then this attribute is not special and should be returned as "ext"
            std::string extKey = oneExt.first;
            std::string extVal = oneExt.second.value;

            json += isExtCommaNeeded ? "," : "";
            json += "{";
            json += utils::json::jsonify(extKey, extVal) + ",\"read_only\" :";
            json += oneExt.second.readOnly ? "true" : "false";
            json += "}";

            isExtCommaNeeded = true;
        } // end of for each loop for ext attribute
    }

    json += "]";
    // Print "ips"

    if (!ips.empty()) {
        json += ", \"ips\" : [";
        size_t i = 1;

        size_t max_size = ips.size();

        for (auto& oneIp : ips) {
            json += "\"" + (oneIp) + "\"";
            json += (max_size == i ? "" : ",");
            i++;
        }
        json += "]";
    }

    // Print "macs"
    if (!macs.empty()) {

        json += ", \"macs\" : [";
        size_t i = 1;

        size_t max_size = macs.size();

        for (auto& oneMac : macs) {
            json += "\"" + (oneMac) + "\"";
            json += (max_size == i ? "" : ",");
            i++;
        }
        json += "]";
    }

    // Print "fqdns"
    if (!fqdns.empty()) {
        json += ", \"fqdns\" : [";
        size_t i        = 1;
        size_t max_size = fqdns.size();
        for (auto& oneFqdn : fqdns) {
            json += "\"" + (oneFqdn) + "\"";
            json += (max_size == i ? "" : ",");
            i++;
        }
        json += "]";
    }

    // Print "fqdns"
    if (!hostnames.empty()) {

        json += " , \"hostnames\" : [";
        size_t i = 1;

        size_t max_size = hostnames.size();
        for (auto& oneHostname : hostnames) {
            json += "\"" + (oneHostname) + "\"";
            json += (max_size == i ? "" : ",");
            i++;
        }
        json += "]";
    }

    // Print "outlets"
    if (!outlets.empty()) {
        json += ", \"outlets\": {";
        size_t i = 1;

        size_t max_size = outlets.size();

        for (auto& oneOutlet : outlets) {

            bool isCommaNeeded = false;

            json += "\"" + (oneOutlet.first) + "\" : [";
            if (!oneOutlet.second.label.empty()) {

                std::string outletLabel = oneOutlet.second.label;

                json += "{\"name\":\"label\",";
                json += utils::json::jsonify("value", outletLabel) + ",\"read_only\" : ";
                json += oneOutlet.second.label_r ? "true" : "false";
                json += "}";
                isCommaNeeded = true;
            }

            if (!oneOutlet.second.group.empty()) {
                if (isCommaNeeded) {
                    json += ",";
                }

                json += "{\"name\":\"group\",";
                json += utils::json::jsonify("value", oneOutlet.second.group) + ",\"read_only\" : ";
                json += oneOutlet.second.group_r ? "true" : "false";
                json += "}";
                isCommaNeeded = true;
            }

            if (!oneOutlet.second.type.empty()) {
                if (isCommaNeeded) {
                    json += ",";
                }

                json += "{\"name\":\"type\",";
                json += utils::json::jsonify("value", oneOutlet.second.type) + ",\"read_only\" : ";
                json += oneOutlet.second.type_r ? "true" : "false";
                json += "}";
            }

            json += "]";
            json += (max_size == i ? "" : ",");
            i++;
        }
        json += "}";
    }

    json += ", \"computed\" : {";
    if (persist::is_rack(tmp->typeId)) {
        int    freeusize         = free_u_size(tmp->id);
        double realpower_nominal = s_rack_realpower_nominal(tmp->name.c_str());

        json += "\"freeusize\":" + (freeusize >= 0 ? std::to_string(freeusize) : "null");
        json +=
            ",\"realpower.nominal\":" + (!std::isnan(realpower_nominal) ? std::to_string(realpower_nominal) : "null");
        json += ", \"outlet.available\" : {";
        std::map<std::string, int> res;
        int                        rv = rack_outlets_available(tmp->id, res);
        if (rv != 0) {
            log_error("Database failure");
            json = "";
            return json;
        }
        size_t i = 1;
        for (const auto& it : res) {

            std::string val   = it.second >= 0 ? std::to_string(it.second) : "null";
            std::string comma = i == res.size() ? "" : ",";
            i++;
            json += "\"" + it.first + "\":" + val;
            json += (comma);
        } // for it : res

        json += "}";
    } // rack

    json += "}}";
    return json;
}

} // namespace fty::asset
