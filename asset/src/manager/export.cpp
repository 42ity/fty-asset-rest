#include "asset/asset-manager.h"
#include <cxxtools/csvserializer.h>
#include <fty/split.h>

namespace fty::asset {

using namespace fmt::literals;

class LineCsvSerializer
{
public:
    explicit LineCsvSerializer(std::ostream& out)
        : _cs{out}
    {
    }

    void add(const std::string& s)
    {
        // escap = if it's the first char to avoid excel command -> Do not care when reimporting
        if (!s.empty() && (s[0] == '=')) {
            _buf.push_back("'" + s);
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

namespace {
    struct Element
    {
        Element()
        {
        }

        Element(const db::WebAssetElement& el)
            : element(&el)
        {
        }

        const db::WebAssetElement* element = nullptr;
        std::vector<Element>       children;
        std::vector<Element*>      links;
        bool                       isExported = false;
    };
} // namespace

class Exporter
{
public:
    AssetExpected<std::string> exportCsv(const std::optional<db::AssetElement>& dc)
    {
        std::stringstream ss;
        LineCsvSerializer lcs(ss);

        if (auto ret = db::maxNumberOfPowerLinks()) {
            m_maxPowerLinks = *ret;
        } else {
            return unexpected(ret.error());
        }

        if (auto ret = db::maxNumberOfAssetGroups()) {
            m_maxGroups = *ret;
        } else {
            return unexpected(ret.error());
        }

        // put all remaining keys from the database
        if (auto ret = updateKeytags(m_keytags); !ret) {
            return unexpected(ret.error());
        }

        // select elements for export
        if (auto ret = fetchElements(dc ? std::optional(dc->id) : std::nullopt); !ret) {
            return unexpected(ret.error());
        }

        // print the first row with names
        createHeader(lcs);

        // export elements
        for (auto& it : m_root.children) {
            if (auto ret = exportRow(it, lcs); !ret) {
                return unexpected(ret.error());
            }
        }

        return ss.str();
    }

private:
    AssetExpected<void> exportRow(Element& el, LineCsvSerializer& lcs)
    {
        if (el.isExported) {
            return {};
        }

        for (auto& ch : el.links) {
            if (auto ret = exportRow(*ch, lcs); !ret) {
                return unexpected(ret.error());
            }
        }

        if (auto ret = exportRow(*el.element, lcs)) {
            for (auto& ch : el.children) {
                if (auto res = exportRow(ch, lcs); !ret) {
                    return unexpected(res.error());
                }
            }
            el.isExported = true;
        } else {
            return unexpected(ret.error());
        }
        return {};
    }

    void createHeader(LineCsvSerializer& lcs)
    {
        // names from asset element table itself
        for (const auto& key : m_assetElementKeytags) {
            if (key == "id") {
                continue; // ugly but works
            }
            lcs.add(key);
        }

        // print power links
        for (uint32_t i = 0; i != m_maxPowerLinks; ++i) {
            lcs.add("power_source.{}"_format(i + 1));
            lcs.add("power_plug_src.{}"_format(i + 1));
            lcs.add("power_input.{}"_format(i + 1));
        }

        // print extended attributes
        for (const auto& k : m_keytags) {
            lcs.add(k);
        }

        // print groups
        for (uint32_t i = 0; i != m_maxGroups; ++i) {
            lcs.add("group.{}"_format(i + 1));
        }

        lcs.add("id");
        lcs.serialize();
    }

    AssetExpected<void> updateKeytags(std::vector<std::string>& keyTags)
    {
        if (auto ret = db::selectExtRwAttributesKeytags()) {
            for (const auto& tag : *ret) {
                auto found = std::find(m_assetElementKeytags.cbegin(), m_assetElementKeytags.cend(), tag);
                if (found != m_assetElementKeytags.end()) {
                    return {};
                }

                if (std::find(keyTags.cbegin(), keyTags.cend(), tag) == keyTags.end()) {
                    keyTags.push_back(tag);
                }
            }
            return {};
        } else {
            return unexpected(ret.error());
        }
    }

    void createTree(Element& parentNode, uint32_t parent)
    {
        for (auto it = m_elements.begin(); it != m_elements.end(); ++it) {
            auto found = std::find_if(it, m_elements.end(), [&](const auto& el) {
                return el.parentId == parent;
            });

            if (found != m_elements.end()) {
                Element ins(*found);
                createTree(ins, found->id);
                parentNode.children.push_back(std::move(ins));
                it = found;
            }
        }
    }

    Expected<Element*> findElement(uint32_t id, Element* parent = nullptr)
    {
        Element& el = parent ? *parent : m_root;
        if (el.element && el.element->id == id) {
            return &el;
        }
        for(auto& ch: el.children) {
            if (auto ret = findElement(id, &ch)) {
                return *ret;
            }
        }
        return unexpected("not found");
    }

    void collectLinks(Element& el)
    {
        if (el.element) {
            auto powerLinks = db::selectAssetDeviceLinksTo(el.element->id, INPUT_POWER_CHAIN);
            if (powerLinks) {
                for (const auto& lnk : *powerLinks) {
                    auto found = findElement(lnk.srcId);
                    if (found) {
                        el.links.push_back(*found);
                    }
                }
            }
        }

        for (auto& it : el.children) {
            collectLinks(it);
        }
    }

    AssetExpected<void> fetchElements(const std::optional<uint32_t>& dc)
    {
        auto res = db::selectAssetElementAll(dc);
        if (!res) {
            return unexpected(res.error());
        }

        m_elements = *res;
        createTree(m_root, dc ? *dc : 0);
        collectLinks(m_root);

        // sort;
        return {};
    }

    AssetExpected<void> fetchAttributes(const db::WebAssetElement& el, db::Attributes& extAttrs)
    {
        auto extAttrsRet = db::selectExtAttributes(el.id);
        if (!extAttrsRet) {
            return unexpected(extAttrsRet.error());
        }
        extAttrs = *extAttrsRet;

        auto it = extAttrs.find("logical_asset");
        if (it != extAttrs.end()) {
            auto extname = db::nameToExtName(it->second.value);
            if (!extname) {
                return unexpected(extname.error());
            }
            extAttrs["logical_asset"] = {*extname, it->second.readOnly};
        }

        return {};
    }

    AssetExpected<void> exportRow(const db::WebAssetElement& el, LineCsvSerializer& lcs)
    {
        db::Attributes extAttrs;
        if (auto ret = fetchAttributes(el, extAttrs); !ret) {
            return unexpected(ret.error());
        }

        std::string location;
        if (auto ret = db::idToNameExtName(el.parentId)) {
            location = ret->second;
        }

        // things from asset element table itself
        lcs.add(el.extName);
        lcs.add(el.typeName);

        std::string subTypeName = el.subtypeName;
        // subtype for groups is stored as ext/type
        if (el.typeName == "group") {
            if (extAttrs.count("type") == 1) {
                subTypeName = extAttrs["type"].value;
                extAttrs.erase("type");
            }
        }

        if (subTypeName == "N_A") {
            subTypeName = "";
        }

        lcs.add(trimmed(subTypeName));
        lcs.add(location);
        lcs.add(el.status);
        lcs.add("P{}"_format(el.priority));
        lcs.add(el.assetTag);

        // power location
        auto powerLinks = db::selectAssetDeviceLinksTo(el.id, INPUT_POWER_CHAIN);
        if (!powerLinks) {
            return unexpected(powerLinks.error());
        }

        for (uint32_t i = 0; i != m_maxPowerLinks; ++i) {
            std::string source;
            std::string plugSrc;
            std::string input;

            if (i >= powerLinks->size()) {
                // nothing here, exists only for consistency reasons
            } else {
                auto rv = db::nameToExtName(powerLinks->at(i).srcName);
                if (!rv) {
                    return unexpected(rv.error());
                }
                source  = *rv;
                plugSrc = powerLinks->at(i).srcSocket;
                input   = std::to_string(powerLinks->at(i).destId);
            }
            lcs.add(source);
            lcs.add(plugSrc);
            lcs.add(input);
        }

        // read-write (!read_only) extended attributes
        for (const auto& k : m_keytags) {
            if (extAttrs.count(k) == 1 && !extAttrs[k].readOnly) {
                lcs.add(extAttrs[k].value);
            } else {
                lcs.add("");
            }
        }

        // groups
        auto groupNames = db::selectGroupNames(el.id);
        if (!groupNames) {
            return unexpected(groupNames.error());
        }

        for (uint32_t i = 0; i != m_maxGroups; i++) {
            if (i >= groupNames->size()) {
                lcs.add("");
            } else {
                if (auto extname = db::nameToExtName(groupNames->at(i))) {
                    lcs.add(*extname);
                } else {
                    return unexpected(extname.error());
                }
            }
        }

        lcs.add(el.name);
        lcs.serialize();
        return {};
    }

private:
    std::vector<std::string> m_keytags = {"description", "ip.1", "company", "site_name", "region", "country", "address",
        "contact_name", "contact_email", "contact_phone", "u_size", "manufacturer", "model", "serial_no", "runtime",
        "installation_date", "maintenance_date", "maintenance_due", "location_u_pos", "location_w_pos",
        "end_warranty_date", "hostname.1", "http_link.1"};

    std::vector<std::string> m_assetElementKeytags = {
        "id", "name", "type", "sub_type", "location", "status", "priority", "asset_tag"};

    std::vector<db::WebAssetElement> m_elements;
    Element                          m_root;

    uint32_t m_maxPowerLinks = 1;
    uint32_t m_maxGroups     = 1;
};

// =====================================================================================================================

AssetExpected<std::string> AssetManager::exportCsv(const std::optional<db::AssetElement>& dc)
{
    Exporter ex;
    return ex.exportCsv(dc);
}

} // namespace fty::asset
