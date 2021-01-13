#include "asset/asset-import.h"
#include "asset/asset-helpers.h"
#include "asset/asset-licensing.h"
#include "asset/csv.h"
#include "asset/db.h"
#include "asset/json.h"
#include "asset/logger.h"
#include <fty/split.h>
#include <fty_asset_activator.h>
#include <fty_common_db_dbpath.h>
#include <fty_log.h>
#include <regex>

#define AGENT_ASSET_ACTIVATOR "etn-licensing-credits"

namespace fty::asset {

// template <typename KT, typename VT>
// std::vector<KT> keys(const std::map<KT, VT>& map)
//{
//    std::vector<KT> keys;
//    transform(begin(map), end(map), back_inserter(keys), [](const auto& pair) {
//        return pair.first;
//    });
//    return keys;
//}

Import::Import(const CsvMap& cm)
    : m_cm(cm)
{
}

const Import::ImportResMap& Import::items() const
{
    return m_el;
}

persist::asset_operation Import::operation() const
{
    return m_operation;
}


std::string Import::mandatoryMissing() const
{
    static std::vector<std::string> mandatory = {"name", "type", "sub_type", "location", "status", "priority"};

    auto all_fields = m_cm.getTitles();
    for (const auto& s : mandatory) {
        if (all_fields.count(s) == 0)
            return s;
    }

    return "";
}

std::map<std::string, std::string> Import::sanitizeRowExtNames(size_t row, bool sanitize) const
{
    static std::vector<std::string>    sanitizeList = {"location", "logical_asset", "power_source.", "group."};
    std::map<std::string, std::string> result;
    // make copy of this one line
    for (auto title : m_cm.getTitles()) {
        result[title] = m_cm.get(row, title);
    }
    if (sanitize) {
        // sanitize ext names to t_bios_asset_element.name
        for (auto item : sanitizeList) {
            if (item[item.size() - 1] == '.') {
                // iterate index .X
                for (int i = 1; true; ++i) {
                    std::string title = item + std::to_string(i);

                    auto it = result.find(title);
                    if (it == result.end()) {
                        break;
                    }

                    auto name = db::extNameToAssetName(it->second);
                    if (!name) {
                        logError(name.error());
                    } else {
                        logDebug("sanitized {} '{}' -> '{}'", title, it->second, *name);
                        result[title] = *name;
                    }
                }
            } else {
                // simple name
                auto it = result.find(item);
                if (it != result.end()) {
                    auto name = db::extNameToAssetName(it->second);
                    if (!name) {
                        logError(name.error());
                    } else {
                        logDebug("sanitized {} '{}' -> '{}'", it->first, it->second, *name);
                        result[item] = *name;
                    }
                }
            }
        }
    }
    return result;
}

uint16_t Import::getPriority(const std::string& s) const
{
    if (s.size() > 2)
        return 5;

    for (size_t i = 0; i != 2; i++) {
        if (s[i] >= 49 && s[i] <= 53) {
            return uint16_t(s[i] - 48);
        }
    }
    return 5;
}

bool Import::checkUSize(const std::string& s) const
{
    static std::regex regex("^[0-9]{1,2}[uU]?$");
    return std::regex_match(s, regex);
}

std::string Import::matchExtAttr(const std::string& value, const std::string& key) const
{
    if (key == "u_size") {
        if (checkUSize(value)) {
            // need to drop the "U"
            std::string tmp = value;
            if (!(::isdigit(tmp.back()))) {
                tmp.pop_back();
            }
            // remove trailing 0
            if (tmp.size() == 2 && tmp[0] == '0') {
                tmp.erase(tmp.begin());
            }
            return tmp;
        } else {
            return {};
        }
    }
    return value;
}

// check if key contains date
// ... warranty_end or ends with _date
bool Import::isDate(const std::string& key) const
{
    static std::regex rex{"(^warranty_end$|_date$)"};
    return std::regex_match(key, rex);
}

AssetExpected<void> Import::process(bool checkLic)
{
    auto m = mandatoryMissing();
    if (m != "") {
        logError("column '{}' is missing, import is aborted", m);
        return unexpected(error(Errors::ParamRequired).format(m));
    }

    std::set<uint32_t> ids;
    if (checkLic) {
        if (auto limitations = getLicensingLimitation(); !limitations) {
            return unexpected(error(Errors::InternalError).format(limitations.error()));
        } else {
            if (!limitations->global_configurability) {
                return unexpected(error(Errors::ActionForbidden)
                                      .format("Asset handling"_tr, "Licensing global_configurability limit hit"_tr));
            }

            for (size_t row = 1; row != m_cm.rows(); ++row) {
                if (auto it = processRow(row, ids, true, checkLic)) {
                    ids.insert(it->id);
                    m_el.emplace(row, *it);
                } else {
                    m_el.emplace(row, unexpected(it.error()));
                }
            }
        }
    } else {
        for (size_t row = 1; row != m_cm.rows(); ++row) {
            if (auto it = processRow(row, ids, true, checkLic)) {
                ids.insert(it->id);
                m_el.emplace(row, *it);
            } else {
                m_el.emplace(row, unexpected(it.error()));
            }
        }
    }
    return {};
}


AssetExpected<db::AssetElement> Import::processRow(size_t row, const std::set<uint32_t>& ids, bool sanitize, bool checkLic)
{
    LOG_START;

    logDebug("################ Row number is {}", row);
    static const std::set<std::string> statuses = {"active", "nonactive", "spare", "retired"};

    auto types = db::readElementTypes();
    if (!types) {
        return unexpected(error(Errors::InternalError).format(types.error()));
    }

    auto subtypes = db::readDeviceTypes();
    if (!subtypes) {
        return unexpected(error(Errors::InternalError).format(subtypes.error()));
    }

    // get location, powersource etc as name from ext.name
    auto sanitizedAssetNames = sanitizeRowExtNames(row, sanitize);

    auto unusedColumns = m_cm.getTitles();
    if (unusedColumns.empty()) {
        return unexpected(error(Errors::BadRequestDocument).format("Cannot import empty document."_tr));
    }

    int rc0 = -1;

    {
        std::string iname = unusedColumns.count("id") ? m_cm.get(1, "id") : "noid";
        if ("rackcontroller-0" == iname) {
            logDebug("RC-0 detected");
            rc0 = 1;
        } else {
            logDebug("RC-0 not detected");
            rc0 = -1;
        }
    }

    // remove the column 'create_mode' which is set to a different value anyway
    if (unusedColumns.count("create_mode")) {
        unusedColumns.erase("create_mode");
    }

    // because id is definitely not an external attribute
    auto idStr = unusedColumns.count("id") ? m_cm.get(row, "id") : "";
    logDebug("id_str = {}, rc_0 = {}", idStr, rc0);

    if (rc0 != int(row) && "rackcontroller-0" == idStr && rc0 != -1) {
        // we got RC-0 but it don't match "myself", change it to something else ("")
        logDebug("RC is marked as rackcontroller-0, but it's not myself");
        idStr = "";
    } else if (rc0 == int(row) && idStr != "rackcontroller-0") {
        logDebug("RC is identified as rackcontroller-0");
        idStr = "rackcontroller-0";
    }

    unusedColumns.erase("id");
    m_operation = persist::asset_operation::INSERT;
    uint32_t id = 0;

    if (!idStr.empty()) {
        if (auto tmp = db::nameToAssetId(idStr)) {
            id = uint32_t(*tmp);
        } else {
            return unexpected(error(Errors::ElementNotFound).format(idStr));
        }
        if (ids.count(id) == 1) {
            return unexpected(
                error(Errors::BadRequestDocument).format("Element id '{}' found twice, aborting"_tr.format(idStr)));
        }
        m_operation = persist::asset_operation::UPDATE;
    }

    auto ename = m_cm.get(row, "name");
    if (ename.empty()) {
        return unexpected(error(Errors::BadParams).format("name", "empty value"_tr, "unique, non empty value"_tr));
    }
    if (ename.length() > 50) {
        return unexpected(
            error(Errors::BadParams).format("name", "too long string"_tr, "unique string from 1 to 50 characters"_tr));
    }

    auto nameRes = db::extNameToAssetName(ename);
    if (!idStr.empty() && nameRes) {
        // internal name from DB must be the same as internal name from CSV
        if (*nameRes != idStr) {
            return unexpected(
                error(Errors::BadParams)
                    .format("name", "already existing name"_tr, "unique string from 1 to 50 characters"_tr));
        }
    }
    std::string name;
    if (nameRes) {
        name = *nameRes;
    }
    //    if (!name) {
    //        return unexpected(name.error());
    //    }
    logDebug("name = '{}/{}'", ename, name);
    unusedColumns.erase("name");

    auto type = m_cm.get_strip(row, "type");
    logDebug("type = '{}'", type);
    if ((*types).find(type) == (*types).end()) {
        std::string received = type.empty() ? "empty value"_tr.toString() : type;
        std::string expected = "[" + implode(*types, ", ", [](const auto& pair) {
            return pair.first;
        }) + "]";
        return unexpected(error(Errors::BadParams).format("type", received, expected));
    }

    uint16_t typeId = uint16_t((*types)[type]);
    unusedColumns.erase("type");

    auto status = m_cm.get_strip(row, "status");
    logDebug("status = '{}'", status);
    if (statuses.find(status) == statuses.end()) {
        std::string received = status.empty() ? "empty value"_tr.toString() : status;
        std::string expected = "[" + implode(statuses, ", ") + "]";
        return unexpected(error(Errors::BadParams).format("status", received, expected));
    }
    unusedColumns.erase("status");

    auto assetTag = unusedColumns.count("asset_tag") ? m_cm.get(row, "asset_tag") : "";
    logDebug("asset_tag = '{}'", assetTag);
    if (assetTag.length() > 50) {
        std::string received = "too long string"_tr;
        std::string expected = "unique string from 1 to 50 characters"_tr;
        return unexpected(error(Errors::BadParams).format("asset_tag", received, expected));
    }
    unusedColumns.erase("asset_tag");

    uint16_t priority = getPriority(m_cm.get_strip(row, "priority"));
    logDebug("priority = {}", priority);
    unusedColumns.erase("priority");

    auto location = sanitizedAssetNames["location"];
    logDebug("location = '{}'", location);
    uint32_t parentId = 0;
    if (!location.empty()) {
        auto ret = db::selectAssetElementByName(location);
        if (ret) {
            parentId = ret->id;
        } else {
            return unexpected(ret.error());
        }
    }
    unusedColumns.erase("location");

    // Business requirement: be able to write 'rack controller', 'RC', 'rc' as subtype == 'rack controller'
    std::map<std::string, int> localSubtypes    = *subtypes;
    int                        rackControllerId = subtypes->find("rack controller")->second;
    int                        patchPanelId     = subtypes->find("patch panel")->second;

    localSubtypes.emplace("rackcontroller", rackControllerId);
    localSubtypes.emplace("rackcontroler", rackControllerId);
    localSubtypes.emplace("rc", rackControllerId);
    localSubtypes.emplace("RC", rackControllerId);
    localSubtypes.emplace("RC3", rackControllerId);

    localSubtypes.emplace(std::make_pair("patchpanel", patchPanelId));

    auto subtype = m_cm.get_strip(row, "sub_type");

    logDebug("subtype = '{}'", subtype);
    if ((type == "device") && (localSubtypes.find(subtype) == localSubtypes.cend())) {
        std::string received = subtype.empty() ? "empty value"_tr.toString() : subtype;
        std::string expected = "[" + implode(*subtypes, ", ", [](const auto& pair) {
            return pair.first;
        }) + "]";
        return unexpected(error(Errors::BadParams).format("subtype", received, expected));
    }

    if ((!subtype.empty()) && (type != "device") && (type != "group")) {
        logWarn("'{}' - subtype is ignored", subtype);
    }

    if ((subtype.empty()) && (type == "group")) {
        return unexpected(error(Errors::ParamRequired).format("subtype (for type group)"_tr));
    }

    uint16_t subtypeId = uint16_t(localSubtypes.find(subtype)->second);
    unusedColumns.erase("sub_type");

    // now we have read all basic information about element
    // if id is set, then it is right time to check what is going on in DB
    if (!idStr.empty()) {
        auto elementInDb = db::selectAssetElementWebById(id);
        if (!elementInDb) {
            return unexpected(elementInDb.error());
        } else {
            if (elementInDb->typeId != typeId) {
                return unexpected(error(Errors::BadRequestDocument).format("Changing of asset type is forbidden"_tr));
            }
            if ((elementInDb->subtypeId != subtypeId) && (elementInDb->subtypeName != "N_A")) {
                return unexpected(
                    error(Errors::BadRequestDocument).format("Changing of asset subtype is forbidden"_tr));
            }
        }
    }

    std::string group;

    // list of element ids of all groups, the element belongs to
    std::set<uint32_t> groups;
    for (int groupIndex = 1; true; groupIndex++) {
        std::string grpColName;
        try {
            // column name
            grpColName = "group." + std::to_string(groupIndex);
            // remove from unused
            unusedColumns.erase(grpColName);
            // take value
            group = sanitizedAssetNames.at(grpColName);
        } catch (const std::out_of_range&) {
            // if column doesn't exist, then break the cycle
            break;
        }
        logDebug("group_name = '{}'", group);
        // if group was not specified, just skip it
        if (!group.empty()) {
            // find an id from DB
            if (auto ret = db::selectAssetElementByName(group)) {
                groups.insert(ret->id); // if OK, then take ID
            } else {
                return unexpected(ret.error());
            }
        }
    }

    std::vector<db::AssetLink> links;
    std::string                linkSource;
    for (int linkIndex = 1; true; linkIndex++) {
        db::AssetLink oneLink;
        std::string   linkColName = "";
        try {
            // column name
            linkColName = "power_source." + std::to_string(linkIndex);
            // remove from unused
            unusedColumns.erase(linkColName);
            // take value
            linkSource = sanitizedAssetNames.at(linkColName);
        } catch (const std::out_of_range&) {
            break;
        }

        // prevent power source being myself
        if (linkSource == ename) {
            logDebug("Ignoring power source=myself");
            linkSource        = "";
            auto linkColName1 = "power_plug_src." + std::to_string(linkIndex);
            auto linkColName2 = "power_input." + std::to_string(linkIndex);
            if (!linkColName1.empty()) {
                unusedColumns.erase(linkColName1);
            }
            if (!linkColName2.empty()) {
                unusedColumns.erase(linkColName2);
            }
            continue;
        }

        logDebug("power_source_name = '{}'", linkSource);
        if (!linkSource.empty()) // if power source is not specified
        {
            // find an id from DB
            if (auto ret = db::selectAssetElementByName(linkSource)) {
                oneLink.src = ret->id; // if OK, then take ID
            } else {
                return unexpected(ret.error());
            }
        }

        // column name
        auto linkColName1 = "power_plug_src." + std::to_string(linkIndex);
        try {
            // remove from unused
            unusedColumns.erase(linkColName1);
            // take value
            auto linkSource1 = m_cm.get(row, linkColName1);
            oneLink.srcOut   = linkSource1.substr(0, 4);
        } catch (const std::out_of_range& e) {
            logDebug("'{}' - is missing at all", linkColName1);
            logDebug(e.what());
        }

        // column name
        auto linkColName2 = "power_input." + std::to_string(linkIndex);
        try {
            unusedColumns.erase(linkColName2);              // remove from unused
            auto linkSource2 = m_cm.get(row, linkColName2); // take value
            oneLink.destIn   = linkSource2.substr(0, 4);
        } catch (const std::out_of_range& e) {
            logDebug("'{}' - is missing at all", linkColName2);
            logDebug(e.what());
        }

        if (oneLink.src != 0) {
            // if first column was ok
            if (type == "device") {
                oneLink.type = 1; // TODO remove hardcoded constant
                links.push_back(oneLink);
            } else {
                logWarn("information about power sources is ignored for type '{}'", type);
            }
        }
    }

    // sanity check, for RC-0 always skip HW attributes
    if (rc0 == int(row) && idStr != "rackcontroller-0") {
        int i;
        // remove all ip.X
        for (i = 1;; ++i) {
            std::string what = "ip." + std::to_string(i);
            if (unusedColumns.count(what)) {
                unusedColumns.erase(what); // remove from unused
            } else {
                break;
            }
        }
        // remove all ipv6.X
        for (i = 1;; ++i) {
            std::string what = "ipv6." + std::to_string(i);
            if (unusedColumns.count(what)) {
                unusedColumns.erase(what); // remove from unused
            } else {
                break;
            }
        }
        // remove fqdn
        if (unusedColumns.count("fqdn")) {
            unusedColumns.erase("fqdn"); // remove from unused
        }
        // remove serial_no
        if (unusedColumns.count("serial_no")) {
            unusedColumns.erase("serial_no"); // remove from unused
        }
        // remove model
        if (unusedColumns.count("model")) {
            unusedColumns.erase("model"); // remove from unused
        }
        // remove manufacturer
        if (unusedColumns.count("manufacturer")) {
            unusedColumns.erase("manufacturer"); // remove from unused
        }
        // remove uuid
        if (unusedColumns.count("uuid")) {
            unusedColumns.erase("uuid"); // remove from unused
        }
    }

    std::map<std::string, std::string> extattributes;
    extattributes["name"] = ename;
    for (auto& key : unusedColumns) {
        // try is not needed, because here are keys that are definitely there
        std::string value = m_cm.get(row, key);

        // BIOS-1564: sanitize the date for warranty_end -- start
        if (isDate(key) && !value.empty()) {
            if (auto date = sanitizeDate(value)) {
                value = *date;
            } else {
                logInfo("Cannot sanitize {} '{}' for device '{}'", key, value, ename);
                return unexpected(error(Errors::BadParams).format(key, value, "ISO date"_tr));
            }
        }

        if (key == "logical_asset" && !value.empty()) {
            // check, that this asset exists
            value = sanitizedAssetNames.at("logical_asset");

            if (auto ret = db::selectAssetElementByName(value); !ret) {
                return unexpected(ret.error());
            }
        } else if ((key == "calibration_offset_t" || key == "calibration_offset_h") && !value.empty()) {
            // we want exceptions to propagate to upper layer
            if (auto ret = sanitizeValueDouble(key, value); !ret) {
                return unexpected(ret.error());
            }
        } else if ((key == "max_current" || key == "max_power") && !value.empty()) {
            // we want exceptions to propagate to upper layer
            if (auto ret = sanitizeValueDouble(key, value); !ret) {
                return unexpected(ret.error());
            } else if (*ret < 0) {
                logInfo("Extattribute: {}='{}' is neither positive not zero", key, value);
                std::string expected = "value must be a not negative number"_tr;
                return unexpected(error(Errors::BadParams).format(key, value, expected));
            }
        }

        if (key == "location_u_pos" && !value.empty()) {
            unsigned long ul = 0;
            try {
                std::size_t pos = 0;
                ul              = std::stoul(value, &pos);
                if (pos != value.length()) {
                    logInfo("Extattribute: {}='{}' is not unsigned integer", key, value);
                    std::string expected = "value must be an unsigned integer"_tr;
                    return unexpected(error(Errors::BadParams).format(key, value, expected));
                }
            } catch (const std::exception&) {
                logInfo("Extattribute: {}='{}' is not unsigned integer", key, value);
                std::string expected = "value must be an unsigned integer"_tr;
                return unexpected(error(Errors::BadParams).format(key, value, expected));
            }
            if (ul == 0 || ul > 52) {
                std::string expected = "value must be between <1, 52>."_tr;
                return unexpected(error(Errors::BadParams).format("location_u_pos", value, expected));
            }
        }

        if (key == "u_size" && !value.empty()) {
            unsigned long ul = 0;
            try {
                std::size_t pos = 0;
                ul              = std::stoul(value, &pos);
                if (pos != value.length()) {
                    logInfo("Extattribute: {}='{}' is not unsigned integer", key, value);
                    std::string expected = "value must be an unsigned integer"_tr;
                    return unexpected(error(Errors::BadParams).format(key, value, expected));
                }
            } catch (const std::exception&) {
                logInfo("Extattribute: {}='{}' is not unsigned integer", key, value);
                std::string expected = "value must be an unsigned integer"_tr;
                return unexpected(error(Errors::BadParams).format("u_size", value, expected));
            }
            if (ul == 0 || ul > 52) {
                std::string expected = "value must be between <1, 52>."_tr;
                return unexpected(error(Errors::BadParams).format("u_size", value, expected));
            }
        }

        if (auto tmp = matchExtAttr(value, key); !tmp.empty()) {
            extattributes[key] = tmp;
        }
    }

    // if the row represents group, the subtype represents a type
    // of the group.
    // As group has no special table as device, then this information
    // sould be inserted as external attribute
    // this parametr is mandatory according rfc-11
    if (type == "group") {
        extattributes["type"] = subtype;
    }

    if (extattributes.find("u_size") != extattributes.end() && extattributes.find("location_u_pos") != extattributes.end()) {
        if (!tryToPlace(parentId, extattributes["u_size"], extattributes["location_u_pos"])) {
            return unexpected(error(Errors::InternalError).format("Asset is not fit"));
        }
    }

    tnt::Connection conn;

    db::AssetElement el;

    if (!idStr.empty()) {
        std::map<std::string, std::string> extattributesRO;
        if (m_cm.getUpdateTs() != "") {
            extattributesRO["update_ts"] = m_cm.getUpdateTs();
        }
        if (m_cm.getUpdateUser() != "") {
            extattributesRO["update_user"] = m_cm.getUpdateUser();
        }
        el.id = id;

        std::string errmsg = "";
        if (type != "device") {
            tnt::Transaction trans(conn);

            auto ret = updateDcRoomRowRackGroup(
                conn, el.id, name, parentId, extattributes, status, priority, groups, assetTag, extattributesRO);

            if (!ret) {
                trans.rollback();
                return unexpected(ret.error());
            } else {
                trans.commit();
            }
        } else {
            if (idStr != "rackcontroller-0") {
                tnt::Transaction trans(conn);

                auto ret = updateDevice(conn, el.id, name, parentId, extattributes, "nonactive", priority, groups,
                    links, assetTag, extattributesRO);

                if (!ret) {
                    trans.rollback();
                    return unexpected(ret.error());
                } else {
                    trans.commit();
                }

                if (type == "device" && status == "active" && subtypeId != rackControllerId && checkLic) {
                    // check if we may activate the device
                    try {
                        std::string assetJson = getJsonAsset(el.id);

                        mlm::MlmSyncClient  client(AGENT_FTY_ASSET, AGENT_ASSET_ACTIVATOR);
                        fty::AssetActivator activationAccessor(client);
                        activationAccessor.activate(assetJson);
                    } catch (const std::exception& e) {
                        return unexpected("licensing-err", e.what());
                    }
                }
            } else {
                tnt::Transaction trans(conn);

                auto ret = updateDevice(conn, el.id, name, parentId, extattributes, status, priority, groups, links,
                    assetTag, extattributesRO);

                if (!ret) {
                    trans.rollback();
                    return unexpected(ret.error());
                } else {
                    trans.commit();
                }
            }
        }
    } else {
        std::map<std::string, std::string> extattributesRO;

        if (m_cm.getCreateMode() != 0) {
            extattributesRO["create_mode"] = std::to_string(m_cm.getCreateMode());
        }

        if (!m_cm.getCreateUser().empty()) {
            extattributesRO["create_user"] = m_cm.getCreateUser();
        }

        if (type != "device") {
            tnt::Transaction trans(conn);
            // this is a transaction
            auto ret = insertDcRoomRowRackGroup(
                conn, ename, typeId, parentId, extattributes, status, priority, groups, assetTag, extattributesRO);

            if (!ret) {
                trans.rollback();
                return unexpected(ret.error());
            } else {
                trans.commit();
                el.id = *ret;
            }
        } else {
            if (subtypeId != rackControllerId) {
                tnt::Transaction trans(conn);

                auto ret = insertDevice(conn, links, groups, ename, parentId, extattributes, subtypeId, "nonactive",
                    priority, assetTag, extattributesRO);

                if (!ret) {
                    trans.rollback();
                    return unexpected(ret.error());
                }
                trans.commit();
                el.id = *ret;

                if (type == "device" && status == "active" && subtypeId != rackControllerId && checkLic) {
                    // check if we may activate the device
                    try {
                        std::string         assetJson = getJsonAsset(el.id);
                        mlm::MlmSyncClient  client(AGENT_FTY_ASSET, AGENT_ASSET_ACTIVATOR);
                        fty::AssetActivator activationAccessor(client);
                        activationAccessor.activate(assetJson);
                    } catch (const std::exception& e) {
                        return unexpected("licensing-err", e.what());
                    }
                }
            } else {
                // this is a transaction
                tnt::Transaction trans(conn);
                auto ret = insertDevice(conn, links, groups, ename, parentId, extattributes, subtypeId, status,
                    priority, assetTag, extattributesRO);

                if (!ret) {
                    trans.rollback();
                    return unexpected(ret.error());
                }
                trans.commit();
                el.id = *ret;
            }
        }
    }

    auto ret = db::extNameToAssetName(ename);
    if (ret) {
        el.name = *ret;
    } else {
        return unexpected("Database failure"_tr);
    }

    el.status    = status;
    el.parentId  = parentId;
    el.priority  = priority;
    el.typeId    = typeId;
    el.subtypeId = subtypeId;
    el.assetTag  = assetTag;
    el.ext       = extattributes;

    return AssetExpected<db::AssetElement>(el);
}

AssetExpected<void> Import::updateDcRoomRowRackGroup(tnt::Connection& conn, uint32_t elementId,
    const std::string& elementName, uint32_t parentId, const std::map<std::string, std::string>& extattributes,
    const std::string& status, uint16_t priority, const std::set<uint32_t>& groups, const std::string& assetTag,
    const std::map<std::string, std::string>& extattributesRO) const
{
    if (elementId == 1 && status == "nonactive") {
        auto msg = "{}: Element cannot be inactivated. Change status to 'active'."_tr.format(elementName);
        logError(msg.toString());
        return unexpected(msg);
    }


    {
        auto ret = db::updateAssetElement(conn, elementId, parentId, status, priority, assetTag);

        if (!ret) {
            auto errmsg = "check  element name, location, status, priority, asset_tag"_tr;
            logError("{}, {}", ret.error(), errmsg);
            return unexpected(errmsg);
        }
    }

    {
        auto ret = db::deleteAssetExtAttributesWithRo(conn, elementId, false);
        if (!ret) {
            auto errmsg = "cannot erase old external attributes"_tr;
            logError("{}: {}", ret.error(), errmsg);
            return unexpected(errmsg);
        }
    }

    {
        auto ret = db::insertIntoAssetExtAttributes(conn, elementId, extattributes, false);
        if (!ret) {
            logError(ret.error());
            return unexpected(ret.error());
        }
    }

    if (!extattributesRO.empty()) {
        auto ret = db::insertIntoAssetExtAttributes(conn, elementId, extattributesRO, false);
        if (!ret) {
            logError(ret.error());
            return unexpected(ret.error());
        }
    }

    {
        auto ret = db::deleteAssetElementFromAssetGroups(conn, elementId);
        if (!ret) {
            logError(ret.error());
            return unexpected(ret.error());
        }
    }

    {
        auto ret = db::insertElementIntoGroups(conn, groups, elementId);
        if (!ret || *ret != groups.size()) {
            auto errmsg = "cannot insert device into all specified groups"_tr;
            logError(errmsg);
            return unexpected(errmsg);
        }
    }

    return {};
}

AssetExpected<void> Import::updateDevice(tnt::Connection& conn, uint32_t elementId, const std::string& elementName,
    uint32_t parentId, const std::map<std::string, std::string>& extattributes, const std::string& status,
    uint16_t priority, const std::set<uint32_t>& groups, const std::vector<db::AssetLink>& links,
    const std::string& assetTag, const std::map<std::string, std::string>& extattributesRO) const
{
    {
        auto ret = updateDcRoomRowRackGroup(
            conn, elementId, elementName, parentId, extattributes, status, priority, groups, assetTag, extattributesRO);
        if (!ret) {
            return unexpected(ret.error());
        }
    }

    auto linksCopy = links;
    // links don't have 'dest' defined - it was not known until now; we have to fix it
    for (auto& oneLink : linksCopy) {
        oneLink.dest = elementId;
    }

    {
        auto ret = db::deleteAssetLinksTo(conn, elementId);
        if (!ret) {
            auto errmsg = "cannot remove old power sources"_tr;
            logError(errmsg);
            return unexpected(errmsg);
        }
    }

    {
        auto ret = db::insertIntoAssetLinks(conn, linksCopy);
        if (!ret) {
            auto errmsg = "cannot add new power sources"_tr;
            logError(errmsg);
            return unexpected(errmsg);
        }
    }

    return {};
}

Expected<uint32_t> Import::insertDcRoomRowRackGroup(tnt::Connection& conn, const std::string& elementName,
    uint16_t elementTypeId, uint32_t parentId, const std::map<std::string, std::string>& extattributes,
    const std::string& status, uint16_t priority, const std::set<uint32_t>& groups, const std::string& assetTag,
    const std::map<std::string, std::string>& extattributesRO) const
{

    if (auto id = db::extNameToAssetId(elementName)) {
        return unexpected(
            "Element '{}' cannot be processed because of conflict. Most likely duplicate entry."_tr.format(
                elementName));
    }

    std::string iname = trimmed(persist::typeid_to_type(elementTypeId));
    logDebug("element_name = '{}/{}'", elementName, iname);

    if (status == "nonactive") {
        return unexpected("Element '{}' cannot be inactivated. Change status to 'active'."_tr.format(elementName));
    }

    uint32_t elementId;
    {
        db::AssetElement el;
        el.name      = iname;
        el.typeId    = elementTypeId;
        el.parentId  = parentId;
        el.status    = status;
        el.priority  = priority;
        el.subtypeId = 0;
        el.assetTag  = assetTag;

        auto ret = db::insertIntoAssetElement(conn, el, false);

        if (!ret) {
            logError(ret.error());
            return unexpected(ret.error());
        }
        elementId = *ret;
    }

    {
        auto ret = db::insertIntoAssetExtAttributes(conn, elementId, extattributes, false);
        if (!ret) {
            logError("device was not inserted (fail in ext_attributes)");
            return unexpected(ret.error());
        }
    }

    if (!extattributesRO.empty()) {
        auto ret = db::insertIntoAssetExtAttributes(conn, elementId, extattributesRO, true);
        if (!ret) {
            logError("device was not inserted (fail in ext_attributes)");
            return unexpected(ret.error());
        }
    }

    {
        auto ret = db::insertElementIntoGroups(conn, groups, elementId);
        if (!ret) {
            logInfo("end: device was not inserted (fail into groups)");
            return unexpected(ret.error());
        }
    }

    if (elementTypeId == persist::asset_type::DATACENTER || elementTypeId == persist::asset_type::RACK) {
        auto ret = db::insertIntoMonitorDevice(conn, 1, elementName);
        if (!ret) {
            logInfo("\"device\" was not inserted (fail monitor_device)");
            return unexpected(ret.error());
        } else {
            auto ret1 = db::insertIntoMonitorAssetRelation(conn, *ret, elementId);
            if (!ret1) {
                logInfo("monitor asset link was not inserted (fail monitor asset relation)");
                return unexpected(ret1.error());
            }
        }
    }
    return elementId;
}

Expected<uint32_t> Import::insertDevice(tnt::Connection& conn, const std::vector<db::AssetLink>& links,
    const std::set<uint32_t>& groups, const std::string& elementName, uint32_t parentId,
    const std::map<std::string, std::string>& extattributes, uint16_t assetDeviceTypeId, const std::string& status,
    uint16_t priority, const std::string& assetTag, const std::map<std::string, std::string>& extattributesRO) const
{
    if (auto ret = db::extNameToAssetId(elementName)) {
        return unexpected(
            "Element '{}' cannot be processed because of conflict. Most likely duplicate entry."_tr.format(
                elementName));
    }

    std::string iname = trimmed(persist::subtypeid_to_subtype(assetDeviceTypeId));
    logDebug("  element_name = '{}/{}'", elementName, iname);

    uint32_t elementId;
    {
        db::AssetElement el;
        el.name      = iname;
        el.typeId    = persist::asset_type::DEVICE;
        el.parentId  = parentId;
        el.status    = status;
        el.priority  = priority;
        el.subtypeId = assetDeviceTypeId;
        el.assetTag  = assetTag;

        auto ret = db::insertIntoAssetElement(conn, el, false);
        if (!ret) {
            logInfo("device was not inserted (fail in element)");
            return unexpected(ret.error());
        }
        elementId = *ret;
    }

    {
        auto ret = db::insertIntoAssetExtAttributes(conn, elementId, extattributes, false);
        if (!ret) {
            logError("device was not inserted (fail in ext_attributes)");
            return unexpected(ret.error());
        }
    }

    if (!extattributesRO.empty()) {
        auto ret = db::insertIntoAssetExtAttributes(conn, elementId, extattributesRO, true);
        if (!ret) {
            logError("device was not inserted (fail in ext_attributes)");
            return unexpected(ret.error());
        }
    }

    {
        auto ret = db::insertElementIntoGroups(conn, groups, elementId);
        if (!ret) {
            logInfo("device was not inserted (fail into groups)");
            return unexpected(ret.error());
        }
    }

    // links don't have 'dest' defined - it was not known until now; we have to fix it
    auto linksCopy = links;
    for (auto& link : linksCopy) {
        link.dest = elementId;
    }

    {
        auto ret = db::insertIntoAssetLinks(conn, linksCopy);
        if (!ret) {
            logInfo("not all links were inserted (fail asset_link)");
            return unexpected(ret.error());
        }
    }

    auto select = db::selectMonitorDeviceTypeId(conn, "not_classified");
    if (select) {
        {
            auto ret = db::insertIntoMonitorDevice(conn, *select, elementName);
            if (!ret) {
                logInfo("device was not inserted (fail monitor_device)");
                return unexpected(ret.error());
            } else {
                auto ret1 = db::insertIntoMonitorAssetRelation(conn, *ret, elementId);
                if (!ret1) {
                    logInfo("monitor asset link was not inserted (fail monitor asset relation)");
                    return unexpected(ret.error());
                }
            }
        }
    } else if (select.error() == "not found") {
        logDebug("device should not being inserted into monitor part");
    } else {
        logWarn("some error in denoting a type of device in monitor part");
        return unexpected(select.error());
    }
    return elementId;
}

bool Import::tryToPlace(uint32_t parentId, const std::string& size, const std::string& loc)
{
    std::cerr << "try to place " << parentId << " size: " << size << " at: " << loc << std::endl;
    auto attr = db::selectExtAttributes(parentId);
    if (!attr) {
        return true;
    }

    std::vector<bool> place;
    place.resize(convert<size_t>(attr->at("u_size").value), false);

    auto children = db::selectAssetsByParent(parentId);
    if (!children) {
        return true;
    }

    for(const auto& child: *children) {
        auto chAttr = db::selectExtAttributes(child);
        if (!chAttr) {
            continue;
        }
        if (!chAttr->count("u_size") && !chAttr->count("location_u_pos")) {
            continue;
        }

        size_t isize = convert<size_t>(chAttr->at("u_size").value);
        size_t iloc = convert<size_t>(chAttr->at("location_u_pos").value)-1;

        for(size_t i = iloc; i < iloc+isize; ++i) {
            if (i < place.size()) {
                place[i] = true;
            }
        }
    }

    std::cerr << implode(place, ",") << std::endl;

    size_t isize = convert<size_t>(size);
    size_t iloc = convert<size_t>(loc)-1;
    for(size_t i = iloc; i < iloc+isize; ++i) {
        if (i >= place.size()) {
            return false;
        }
        if (place[i]) {
            return false;
        }
    }

    return true;
}

} // namespace fty::asset
