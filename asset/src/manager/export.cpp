#include "asset/asset-manager.h"
#include <cxxtools/csvserializer.h>
#include <fty/split.h>

namespace fty::asset {

static AssetExpected<void> updateKeytags(const std::vector<std::string>& aek, std::vector<std::string>& s)
{
    if (auto ret = db::selectExtRwAttributesKeytags()) {
        for (const auto& tag : *ret) {
            if (std::find(aek.cbegin(), aek.cend(), tag) != aek.end()) {
                return {};
            }

            if (std::find(s.cbegin(), s.cend(), tag) == s.end()) {
                s.push_back(tag);
            }
        }
        return {};
    } else {
        return unexpected(ret.error());
    }
}

class LineCsvSerializer
{
public:
    explicit LineCsvSerializer(std::ostream& out)
        : _cs{out}
    {
    }

    void add(const std::string& s)
    {
        //escap = if it's the first char to avoid excel command -> Do not care when reimporting
        if (!s.empty() && (s[0] == '=')) {
            _buf.push_back("'"+s);
        } else {
            _buf.push_back(s);
        }
    }

    void add(const uint32_t i)
    {
        return add(std::to_string(i));
    }

    void serialize()
    {
        std::vector<std::vector<std::string>> aux{};
        aux.push_back(_buf);
        _cs.serialize(aux);
        _buf.clear();
    }

protected:
    cxxtools::CsvSerializer  _cs;
    std::vector<std::string> _buf;
};

AssetExpected<std::string> AssetManager::exportCsv(const std::optional<db::AssetElement>& dc)
{
    std::stringstream ss;
    LineCsvSerializer lcs(ss);

    // TODO: move somewhere else
    std::vector<std::string> KEYTAGS = {"description", "ip.1", "company", "site_name", "region", "country", "address",
        "contact_name", "contact_email", "contact_phone", "u_size", "manufacturer", "model", "serial_no", "runtime",
        "installation_date", "maintenance_date", "maintenance_due", "location_u_pos", "location_w_pos",
        "end_warranty_date", "hostname.1", "http_link.1"};

    static std::vector<std::string> ASSET_ELEMENT_KEYTAGS = {
        "id", "name", "type", "sub_type", "location", "status", "priority", "asset_tag"};

    uint32_t max_power_links = 1;
    if (auto ret = db::maxNumberOfPowerLinks()) {
        max_power_links = *ret;
    } else {
        return unexpected(ret.error());
    }

    uint32_t max_groups = 1;
    if (auto ret = db::maxNumberOfAssetGroups()) {
        max_groups = *ret;
    } else {
        return unexpected(ret.error());
    }

    // put all remaining keys from the database
    if (auto rv = updateKeytags(ASSET_ELEMENT_KEYTAGS, KEYTAGS); !rv) {
        return unexpected(rv.error());
    }

    // 1 print the first row with names
    // 1.1      names from asset element table itself
    for (const auto& k : ASSET_ELEMENT_KEYTAGS) {
        if (k == "id") {
            continue; // ugly but works
        }
        lcs.add(k);
    }

    // 1.2      print power links
    for (uint32_t i = 0; i != max_power_links; ++i) {
        std::string si = std::to_string(i + 1);
        lcs.add("power_source." + si);
        lcs.add("power_plug_src." + si);
        lcs.add("power_input." + si);
    }

    // 1.3      print extended attributes
    for (const auto& k : KEYTAGS) {
        lcs.add(k);
    }

    // 1.4      print groups
    for (uint32_t i = 0; i != max_groups; ++i) {
        std::string si = std::to_string(i + 1);
        lcs.add("group." + si);
    }

    lcs.add("id");
    lcs.serialize();

    auto res = db::selectAssetElementAll(dc ? std::optional(dc->id) : std::nullopt);
    if (!res) {
        return unexpected(res.error());
    }

    for (const db::WebAssetElement& el : *res) {
        std::string location;
        if (auto ret = db::idToNameExtName(el.parentId)) {
            location = ret->second;
        }

        auto extAttrsRet = db::selectExtAttributes(el.id);
        if (!extAttrsRet) {
            return unexpected(extAttrsRet.error());
        }

        auto ext_attrs = std::move(*extAttrsRet);

        // 2.5      PRINT IT
        // 2.5.1    things from asset element table itself
        // ORDER of fields added to the lcs IS SIGNIFICANT
        lcs.add(el.extName);
        lcs.add(el.typeName);

        std::string subtype_name = el.subtypeName;
        // subtype for groups is stored as ext/type
        if (el.typeName == "group") {
            if (ext_attrs.count("type") == 1) {
                subtype_name = ext_attrs["type"].value;
                ext_attrs.erase("type");
            }
        }
        if (subtype_name == "N_A") {
            subtype_name = "";
        }

        lcs.add(trimmed(subtype_name));
        lcs.add(location);
        lcs.add(el.status);
        lcs.add("P" + std::to_string(el.priority));
        lcs.add(el.assetTag);

        // 2.5.2        power location
        auto power_links = db::selectAssetDeviceLinksTo(el.id, INPUT_POWER_CHAIN);
        if (!power_links) {
            return unexpected(power_links.error());
        }

        for (uint32_t i = 0; i != max_power_links; ++i) {
            std::string source;
            std::string plug_src;
            std::string input;

            if (i >= power_links->size()) {
                // nothing here, exists only for consistency reasons
            } else {
                auto rv = db::nameToExtName(power_links->at(i).srcName);
                if (!rv) {
                    return unexpected(rv.error());
                }
                source   = *rv;
                plug_src = power_links->at(i).srcSocket;
                input    = std::to_string(power_links->at(i).destId);
            }
            lcs.add(source);
            lcs.add(plug_src);
            lcs.add(input);
        }

        // convert necessary ids to names, for now just logical_asset
        {
            auto it = ext_attrs.find("logical_asset");
            if (it != ext_attrs.end()) {
                auto extname = db::nameToExtName(it->second.value);
                if (!extname) {
                    return unexpected(extname.error());
                }
                ext_attrs["logical_asset"] = {*extname, it->second.readOnly};
            }
        }

        // 2.5.3        read-write (!read_only) extended attributes
        for (const auto& k : KEYTAGS) {
            if (ext_attrs.count(k) == 1 && !ext_attrs[k].readOnly) {
                lcs.add(ext_attrs[k].value);
            } else {
                lcs.add("");
            }
        }

        // 2.5.4        groups
        auto groupNames = db::selectGroupNames(el.id);
        if (!groupNames) {
            return unexpected(groupNames.error());
        }

        for (uint32_t i = 0; i != max_groups; i++) {
            if (i >= groupNames->size()) {
                lcs.add("");
            } else {
                auto extname = db::nameToExtName(groupNames->at(i));
                if (!extname) {
                    return unexpected(extname.error());
                }
                lcs.add(*extname);
            }
        }

        lcs.add(el.name);
        lcs.serialize();
    }

    return ss.str();
}

}
