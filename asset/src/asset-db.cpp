#include "asset/asset-db.h"
#include "asset/db.h"
#include "asset/error.h"
#include "asset/logger.h"
#include <fty/split.h>
#include <fty/translate.h>
#include <fty_common_asset_types.h>
#include <sys/time.h>

#define MAX_CREATE_RETRY 10

namespace fty::asset::db {

// =====================================================================================================================

static std::string webAssetSql()
{
    static const std::string sql = R"(
        SELECT
            v.id                as id,
            v.name              as name,
            ext.value           as extName,
            v.id_type           as typeId,
            v.type_name         as typeName,
            v.subtype_id        as subTypeId,
            v.subtype_name      as subTypeName,
            v.id_parent         as parentId,
            v.id_parent_type    as parentTypeId,
            v.parent_name       as parentName,
            v.status            as status,
            v.priority          as priority,
            v.asset_tag         as assetTag
        FROM
            v_web_element v
        LEFT JOIN
                t_bios_asset_ext_attributes AS ext
            ON
                ext.id_asset_element = v.id AND ext.keytag = "name"
    )";
    return sql;
}

static void fetchWebAsset(const tnt::Row& row, WebAssetElement& asset)
{
    row.get("id", asset.id);
    row.get("name", asset.name);
    row.get("extName", asset.extName);
    row.get("typeId", asset.typeId);
    row.get("typeName", asset.typeName);
    row.get("subTypeId", asset.subtypeId);
    row.get("subTypeName", asset.subtypeName);
    row.get("parentId", asset.parentId);
    row.get("parentTypeId", asset.parentTypeId);
    row.get("parentName", asset.parentName);
    row.get("status", asset.status);
    row.get("priority", asset.priority);
    row.get("assetTag", asset.assetTag);
}

// =====================================================================================================================

Expected<int64_t> nameToAssetId(const std::string& assetName)
{
    static const std::string sql = R"(
        SELECT
            id_asset_element
        FROM
            t_bios_asset_element
        WHERE name = :assetName
    )";

    try {
        tnt::Connection db;

        auto res = db.selectRow(sql, "assetName"_p = assetName);

        return res.get<int64_t>("id_asset_element");
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(assetName));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), assetName));
    }
}

// =====================================================================================================================

Expected<std::pair<std::string, std::string>> idToNameExtName(uint32_t assetId)
{
    static std::string sql = R"(
        SELECT asset.name, ext.value
        FROM
            t_bios_asset_element AS asset
        LEFT JOIN
            t_bios_asset_ext_attributes AS ext
        ON
            ext.id_asset_element = asset.id_asset_element
        WHERE
            ext.keytag = "name" AND asset.id_asset_element = :assetId
    )";

    try {
        tnt::Connection db;

        auto res = db.selectRow(sql, "assetId"_p = assetId);

        return std::make_pair(res.get<std::string>("name"), res.get<std::string>("value"));
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(assetId));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), assetId));
    }
}

// =====================================================================================================================

Expected<std::string> nameToExtName(std::string assetName)
{
    static const std::string sql = R"(
        SELECT e.value
        FROM t_bios_asset_ext_attributes AS e
        INNER JOIN t_bios_asset_element AS a
            ON a.id_asset_element = e.id_asset_element
        WHERE keytag = 'name' AND a.name = :asset_name
    )";

    try {
        tnt::Connection conn;
        auto            res = conn.selectRow(sql, "asset_name"_p = assetName);
        return res.get("value");
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(assetName));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), assetName));
    }
}

// =====================================================================================================================

Expected<std::string> extNameToAssetName(const std::string& assetExtName)
{
    static const std::string sql = R"(
        SELECT a.name
        FROM t_bios_asset_element AS a
        INNER JOIN
            t_bios_asset_ext_attributes AS e
        ON
            a.id_asset_element = e.id_asset_element
        WHERE
            keytag = "name" and value = :extName
    )";

    try {
        tnt::Connection db;

        auto res = db.selectRow(sql, "extName"_p = assetExtName);

        return res.get("name");
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(assetExtName));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), assetExtName));
    }
}

// =====================================================================================================================

Expected<int64_t> extNameToAssetId(const std::string& assetExtName)
{
    static const std::string sql = R"(
        SELECT
            a.id_asset_element
        FROM
            t_bios_asset_element AS a
        INNER JOIN
            t_bios_asset_ext_attributes AS e
        ON
            a.id_asset_element = e.id_asset_element
        WHERE
            keytag = 'name' and value = :extName
    )";
    try {
        tnt::Connection db;

        auto res = db.selectRow(sql, "extName"_p = assetExtName);

        return res.get<int64_t>("id_asset_element");
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(assetExtName));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), assetExtName));
    }
}

// =====================================================================================================================

Expected<AssetElement> selectAssetElementByName(const std::string& elementName)
{
    static const std::string nameSql = R"(
        SELECT
            v.name, v.id_parent, v.status, v.priority, v.id, v.id_type, v.id_subtype
        FROM
            v_bios_asset_element v
        WHERE v.name = :name
    )";

    static const std::string extNameSql = R"(
        SELECT v.name, v.id_parent, v.status, v.priority, v.id, v.id_type, v.id_subtype
        FROM
            v_bios_asset_element AS v
        LEFT JOIN
            v_bios_asset_ext_attributes AS ext
        ON
            ext.id_asset_element = v.id
        WHERE
            ext.keytag = 'name' AND ext.value = :name
    )";

    if (!persist::is_ok_name(elementName.c_str())) {
        return unexpected("name is not valid"_tr);
    }

    try {
        tnt::Connection db;
        tnt::Row        row;

        try {
            row = db.selectRow(nameSql, "name"_p = elementName);
        } catch (const tntdb::NotFound&) {
            row = db.selectRow(extNameSql, "name"_p = elementName);
        }

        AssetElement el;
        row.get("name", el.name);
        row.get("id_parent", el.parentId);
        row.get("status", el.status);
        row.get("priority", el.priority);
        row.get("id", el.id);
        row.get("id_type", el.typeId);
        row.get("id_subtype", el.subtypeId);

        return std::move(el);
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(elementName));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementName));
    }
}

// =====================================================================================================================

Expected<WebAssetElement> selectAssetElementWebById(uint32_t elementId)
{
    WebAssetElement el;

    if (auto ret = selectAssetElementWebById(elementId, el)) {
        return std::move(el);
    } else {
        return unexpected(ret.error());
    }
}

// =====================================================================================================================

Expected<void> selectAssetElementWebById(uint32_t elementId, WebAssetElement& asset)
{
    static const std::string sql = webAssetSql() + R"(
        WHERE
            :id = v.id
    )";

    try {
        tnt::Connection db;

        auto row = db.selectRow(sql, "id"_p = elementId);

        fetchWebAsset(row, asset);

        return {};
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(elementId));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<Attributes> selectExtAttributes(uint32_t elementId)
{
    static const std::string sql = R"(
        SELECT
            v.keytag,
            v.value,
            v.read_only
        FROM
            v_bios_asset_ext_attributes v
        WHERE
            v.id_asset_element = :elementId
    )";

    try {
        tnt::Connection db;

        auto result = db.select(sql, "elementId"_p = elementId);

        Attributes attrs;

        for (const auto& row : result) {
            ExtAttrValue val;

            row.get("value", val.value);
            row.get("read_only", val.readOnly);

            attrs.emplace(row.get("keytag"), val);
        }

        return std::move(attrs);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<std::map<uint32_t, std::string>> selectAssetElementGroups(uint32_t elementId)
{
    static std::string sql = R"(
        SELECT
            v1.id_asset_group, v.name
        FROM
            v_bios_asset_group_relation v1,
            v_bios_asset_element v
        WHERE
            v1.id_asset_element = :idelement AND
            v.id = v1.id_asset_group
    )";


    try {
        tnt::Connection db;

        auto res = db.select(sql, "idelement"_p = elementId);

        std::map<uint32_t, std::string> item;

        for (const auto& row : res) {
            item.emplace(row.get<uint32_t>("id_asset_group"), row.get("name"));
        }
        return std::move(item);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<uint> updateAssetElement(tnt::Connection& db, uint32_t elementId, uint32_t parentId, const std::string& status,
    uint16_t priority, const std::string& assetTag)
{
    static const std::string sql = R"(
        UPDATE
            t_bios_asset_element
        SET
            asset_tag = :assetTag,
            id_parent = :parentId,
            status    = :status,
            priority  = :priority
        WHERE
            id_asset_element = :id
    )";

    try {
        auto st = db.prepare(sql);

        // clang-format off
        st.bind(
            "id"_p        = elementId,
            "status"_p    = status,
            "priority"_p  = priority,
            "assetTag"_p  = assetTag,
            "parentId"_p  = nullable(parentId, parentId)// : std::nullopt//tnt::empty<uint32_t>
        );
        // clang-format on

        return st.execute();
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<uint> deleteAssetExtAttributesWithRo(tnt::Connection& conn, uint32_t elementId, bool readOnly)
{
    static const std::string sql = R"(
        DELETE FROM
            t_bios_asset_ext_attributes
        WHERE
            id_asset_element = :element AND
            read_only = :ro
    )";

    try {
        // clang-format off
        return conn.execute(sql,
            "element"_p = elementId,
            "ro"_p      = readOnly
        );
        // clang-format on
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<uint> insertIntoAssetExtAttributes(
    tnt::Connection& conn, uint32_t elementId, const std::map<std::string, std::string>& attributes, bool readOnly)
{
    const std::string sql = fmt::format(R"(
        INSERT INTO
            t_bios_asset_ext_attributes (keytag, value, id_asset_element, read_only)
        VALUES
            {}
        ON DUPLICATE KEY UPDATE
            id_asset_ext_attribute = LAST_INSERT_ID(id_asset_ext_attribute)
    )",
        tnt::multiInsert({"keytag", "value", "id_asset_element", "read_only"}, attributes.size()));

    if (attributes.empty()) {
        return unexpected("no attributes to insert"_tr);
    }

    try {
        auto st = conn.prepare(sql);

        size_t count = 0;
        for (const auto& [key, value] : attributes) {
            // clang-format off
            st.bindMulti(count++,
                "keytag"_p           = key,
                "value"_p            = value,
                "id_asset_element"_p = elementId,
                "read_only"_p        = readOnly
            );
            // clang-format on
        }

        return st.execute();
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<uint> deleteAssetElementFromAssetGroups(tnt::Connection& conn, uint32_t elementId)
{
    static const std::string sql = R"(
        DELETE FROM
            t_bios_asset_group_relation
        WHERE
            id_asset_element = :elementId
    )";

    try {
        return conn.execute(sql, "elementId"_p = elementId);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<uint> insertElementIntoGroups(tnt::Connection& conn, const std::set<uint32_t>& groups, uint32_t elementId)
{
    // input parameters control
    if (elementId == 0) {
        auto msg = fmt::format("{}, {}", "ignore insert"_tr, "0 value of asset_element_id is not allowed"_tr);
        logError(msg);
        return unexpected(msg);
    }

    if (groups.empty()) {
        logDebug("nothing to insert");
        return 0;
    }

    const std::string sql = fmt::format(R"(
        INSERT INTO
            t_bios_asset_group_relation
            (id_asset_group, id_asset_element)
         VALUES {}
    )",
        tnt::multiInsert({"gid", "elementId"}, groups.size()));

    try {
        auto   st    = conn.prepare(sql);
        size_t count = 0;
        for (uint32_t gid : groups) {
            // clang-format off
            st.bindMulti(count++,
                "gid"_p       = gid,
                "elementId"_p = elementId
            );
            // clang-format on
        }

        uint affectedRows = st.execute();

        if (affectedRows == groups.size()) {
            return affectedRows;
        } else {
            auto msg = "not all links were inserted"_tr;
            logError(msg.toString());
            return unexpected(msg);
        }
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

static std::string createAssetName(tnt::Connection& conn, uint32_t typeId, uint32_t subtypeId)
{
    std::string type    = persist::typeid_to_type(static_cast<uint16_t>(typeId));
    std::string subtype = persist::subtypeid_to_subtype(static_cast<uint16_t>(subtypeId));

    std::string assetName;
    timeval     t;

    bool        valid = false;
    std::string indexStr;

    unsigned retry = 0;

    while (!valid && (retry++ < MAX_CREATE_RETRY)) {
        gettimeofday(&t, nullptr);
        srand(static_cast<unsigned int>(t.tv_sec * t.tv_usec));
        // generate 8 digit random integer
        unsigned long index = static_cast<unsigned long>(rand()) % static_cast<unsigned long>(100000000);

        indexStr = std::to_string(index);
        // create 8 digit index with leading zeros
        indexStr = std::string(8 - indexStr.length(), '0') + indexStr;

        static const std::string sql = R"(
            SELECT COUNT(id_asset_element) as cnt
            FROM t_bios_asset_element
            WHERE name like :name
        )";

        logDebug("Checking ID {} validity", indexStr);
        try {
            auto res = conn.selectRow(sql, "name"_p = std::string("%").append(indexStr));
            valid    = (res.get<unsigned>("cnt") == 0);
        } catch (const std::exception& e) {
            throw std::runtime_error(e.what());
        }
    }

    if (!valid) {
        throw std::runtime_error("Multiple Asset ID collisions - impossible to create asset");
    }

    if (typeId == persist::DEVICE) {
        assetName = subtype + "-" + indexStr;
    } else {
        assetName = type + "-" + indexStr;
    }

    return assetName;
}


// =====================================================================================================================

Expected<uint32_t> insertIntoAssetElement(tnt::Connection& conn, const AssetElement& element, bool update)
{
    if (!persist::is_ok_name(element.name.c_str())) {
        return unexpected("wrong element name"_tr);
    }

    if (!persist::is_ok_element_type(element.typeId)) {
        return unexpected("{} value of element_type_id is not allowed"_tr.format(element.typeId));
    }

    if (element.typeId == persist::asset_type::DATACENTER && element.parentId != 0) {
        return unexpected("cannot inset datacenter"_tr);
    }

    static const std::string sql = R"(
        INSERT INTO t_bios_asset_element
            (name, id_type, id_subtype, id_parent, status, priority, asset_tag)
        VALUES
            (:name, :typeId, :subtypeId, :parentId, :status, :priority, :assetTag)
        ON DUPLICATE KEY UPDATE name = :name
    )";

    try {
        auto st = conn.prepare(sql);
        // clang-format off
        st.bind(
            "name"_p      = update ? element.name : createAssetName(conn, element.typeId, element.subtypeId),
            "typeId"_p    = element.typeId,
            "subtypeId"_p = element.subtypeId != 0 ? element.subtypeId : uint32_t(persist::asset_subtype::N_A),
            "status"_p    = element.status,
            "priority"_p  = element.priority,
            "assetTag"_p  = element.assetTag,
            "parentId"_p  = nullable(element.parentId, element.parentId)
        );
        // clang-format on
        uint affectedRows = st.execute();

        uint32_t rowid = uint32_t(conn.lastInsertId());

        if (affectedRows == 0) {
            return unexpected("Something going wrong");
        }

        return rowid;
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), element.name));
    }
}

// =====================================================================================================================

Expected<uint> deleteAssetLinksTo(tnt::Connection& conn, uint32_t elementId)
{
    static const std::string sql = R"(
        DELETE FROM
            t_bios_asset_link
        WHERE
            id_asset_device_dest = :dest
    )";

    try {
        return conn.execute(sql, "dest"_p = elementId);
    } catch (const std::exception& e) {
        return unexpected(e.what());
    }
}

// =====================================================================================================================

Expected<uint> insertIntoAssetLinks(tnt::Connection& conn, const std::vector<AssetLink>& links)
{
    if (links.empty()) {
        logDebug("nothing to insert");
        return 0;
    }

    uint affectedRows = 0;
    for (auto& link : links) {
        auto ret = insertIntoAssetLink(conn, link);

        if (ret) {
            affectedRows++;
        }
    }

    if (affectedRows == links.size()) {
        return affectedRows;
    } else {
        logError("not all links were inserted");
        return unexpected("not all links were inserted");
    }
}

// =====================================================================================================================

Expected<int64_t> insertIntoAssetLink(tnt::Connection& conn, const AssetLink& link)
{
    // input parameters control
    if (link.dest == 0) {
        logError("ignore insert: destination device is not specified");
        return unexpected("destination device is not specified");
    }

    if (link.src == 0) {
        logError("ignore insert: source device is not specified");
        return unexpected("source device is not specified");
    }

    if (!persist::is_ok_link_type(uint8_t(link.type))) {
        logError("ignore insert: wrong link type");
        return unexpected("wrong link type");
    }

    if (link.src == link.dest) {
        logError("ignore insert: connection loop");
        return unexpected("connection loop was detected");
    }

    static const std::string sql = R"(
        INSERT INTO
            t_bios_asset_link
            (id_asset_device_src, id_asset_device_dest, id_asset_link_type, src_out, dest_in)
        SELECT
            v1.id_asset_element, v2.id_asset_element, :linktype, :out, :in
        FROM
            v_bios_asset_device v1,
            v_bios_asset_device v2
        WHERE
            v1.id_asset_element = :src AND
            v2.id_asset_element = :dest AND
        NOT EXISTS (
            SELECT
                id_link
            FROM
                t_bios_asset_link v3
            WHERE
                v3.id_asset_device_src = v1.id_asset_element AND
                v3.id_asset_device_dest = v2.id_asset_element AND
                ( ((v3.src_out = :out) AND (v3.dest_in = :in)) ) AND
                v3.id_asset_device_dest = v2.id_asset_element
        )
    )";

    try {
        // clang-format off
        conn.execute(sql,
            "src"_p      = link.src,
            "dest"_p     = link.dest,
            "linktype"_p = link.type,
            "out"_p      = nullable(!link.srcOut.empty(), link.srcOut),
            "in"_p       = nullable(!link.destIn.empty(), link.destIn)
        );
        // clang-format on

        return conn.lastInsertId();
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), link.src));
    }
}

// =====================================================================================================================

Expected<uint16_t> insertIntoMonitorDevice(tnt::Connection& conn, uint16_t deviceTypeId, const std::string& deviceName)
{
    static const std::string sql = R"(
        INSERT INTO t_bios_discovered_device
            (name, id_device_type)
        VALUES
            (:name, :deviceTypeId)
        ON DUPLICATE KEY
        UPDATE
            id_discovered_device = LAST_INSERT_ID(id_discovered_device)
    )";

    try {
        conn.execute(sql, "name"_p = deviceName, "deviceTypeId"_p = deviceTypeId);
        return uint16_t(conn.lastInsertId());
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), deviceTypeId));
    }
}

// =====================================================================================================================

Expected<int64_t> insertIntoMonitorAssetRelation(tnt::Connection& conn, uint16_t monitorId, uint32_t elementId)
{
    if (elementId == 0) {
        auto msg = "0 value of elementId is not allowed"_tr;
        logError("ignore insert, {}", "ignore insert", msg);
        return unexpected(msg);
    }

    if (monitorId == 0) {
        auto msg = "0 value of monitorId is not allowed"_tr;
        logError("ignore insert, {}", "ignore insert", msg);
        return unexpected(msg);
    }

    static const std::string sql = R"(
        INSERT INTO t_bios_monitor_asset_relation
            (id_discovered_device, id_asset_element)
        VALUES
            (:monitor, :asset)
    )";

    try {
        // clang-format off
        conn.execute(sql,
            "monitor"_p = monitorId,
            "asset"_p   = elementId
        );
        // clang-format on
        return conn.lastInsertId();
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), monitorId));
    }
}

// =====================================================================================================================

Expected<uint16_t> selectMonitorDeviceTypeId(tnt::Connection& conn, const std::string& deviceTypeName)
{
    static const std::string sql = R"(
        SELECT
            v.id
        FROM
            v_bios_device_type v
        WHERE
            v.name = :name
    )";

    try {
        auto res = conn.selectRow(sql, "name"_p = deviceTypeName);
        return res.get<uint16_t>("id");
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(deviceTypeName));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), deviceTypeName));
    }
}

// =====================================================================================================================

Expected<void> selectAssetElementSuperParent(uint32_t id, SelectCallback&& cb)
{
    static const std::string sql = R"(
        SELECT
            v.id_asset_element      as id,
            v.id_parent1            as id_parent1,
            v.id_parent2            as id_parent2,
            v.id_parent3            as id_parent3,
            v.id_parent4            as id_parent4,
            v.id_parent5            as id_parent5,
            v.id_parent6            as id_parent6,
            v.id_parent7            as id_parent7,
            v.id_parent8            as id_parent8,
            v.id_parent9            as id_parent9,
            v.id_parent10           as id_parent10,
            v.name_parent1          as parent_name1,
            v.name_parent2          as parent_name2,
            v.name_parent3          as parent_name3,
            v.name_parent4          as parent_name4,
            v.name_parent5          as parent_name5,
            v.name_parent6          as parent_name6,
            v.name_parent7          as parent_name7,
            v.name_parent8          as parent_name8,
            v.name_parent9          as parent_name9,
            v.name_parent10         as parent_name10,
            v.id_type_parent1       as id_type_parent1,
            v.id_type_parent2       as id_type_parent2,
            v.id_type_parent3       as id_type_parent3,
            v.id_type_parent4       as id_type_parent4,
            v.id_type_parent5       as id_type_parent5,
            v.id_type_parent6       as id_type_parent6,
            v.id_type_parent7       as id_type_parent7,
            v.id_type_parent8       as id_type_parent8,
            v.id_type_parent9       as id_type_parent9,
            v.id_type_parent10      as id_type_parent10,
            v.id_subtype_parent1    as id_subtype_parent1,
            v.id_subtype_parent2    as id_subtype_parent2,
            v.id_subtype_parent3    as id_subtype_parent3,
            v.id_subtype_parent4    as id_subtype_parent4,
            v.id_subtype_parent5    as id_subtype_parent5,
            v.id_subtype_parent6    as id_subtype_parent6,
            v.id_subtype_parent7    as id_subtype_parent7,
            v.id_subtype_parent8    as id_subtype_parent8,
            v.id_subtype_parent9    as id_subtype_parent9,
            v.id_subtype_parent10   as id_subtype_parent10,
            v.name                  as name,
            v.type_name             as type_name,
            v.id_asset_device_type  as device_type,
            v.status                as status,
            v.asset_tag             as asset_tag,
            v.priority              as priority,
            v.id_type               as id_type
        FROM
            v_bios_asset_element_super_parent AS v
        WHERE
            v.id_asset_element = :id
    )";

    try {
        tnt::Connection conn;
        for (const auto& row : conn.select(sql, "id"_p = id)) {
            cb(row);
        }
        return {};
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), id));
    }
}

// =====================================================================================================================

Expected<void> selectAssetsByContainer(tnt::Connection& conn, uint32_t elementId, std::vector<uint16_t> types,
    std::vector<uint16_t> subtypes, const std::string& without, const std::string& status, SelectCallback&& cb)
{
    std::string select = R"(
        SELECT
            v.name,
            v.id_asset_element     as asset_id,
            v.id_asset_device_type as subtype_id,
            v.type_name            as subtype_name,
            v.id_type              as type_id
        FROM
            v_bios_asset_element_super_parent AS v
        WHERE
            :containerid in (
                v.id_parent1, v.id_parent2, v.id_parent3,
                v.id_parent4, v.id_parent5, v.id_parent6,
                v.id_parent7, v.id_parent8, v.id_parent9,
                v.id_parent10
            )
    )";

    if (!subtypes.empty()) {
        std::string list = implode(subtypes, ", ", [](const auto& it) {
            return std::to_string(it);
        });
        select += " AND v.id_asset_device_type in (" + list + ")";
    }

    if (!types.empty()) {
        std::string list = implode(types, ", ", [](const auto& it) {
            return std::to_string(it);
        });
        select += " AND v.id_type in (" + list + ")";
    }

    if (!status.empty()) {
        select += " AND v.status = \"" + status + "\"";
    }

    if (!without.empty()) {
        if (without == "location") {
            select += " AND v.id_parent1 is NULL ";
        } else if (without == "powerchain") {
            select += R"(
                AND NOT EXISTS
                (
                    SELECT
                        id_asset_device_dest
                    FROM
                        t_bios_asset_link_type as l
                    JOIN t_bios_asset_link as a
                        ON a.id_asset_link_type=l.id_asset_link_type
                    WHERE
                        name="power chain" AND
                        v.id_asset_element=a.id_asset_device_dest
                )
            )";
        } else {
            select += R"(
                AND NOT EXISTS
                (
                    SELECT a.id_asset_element
                    FROM
                        t_bios_asset_ext_attributes as a
                    WHERE
                        a.keytag=")" +
                      without + R"(" AND v.id_asset_element = a.id_asset_element
                )
            )";
        }
    }

    try {
        for (const auto& row : conn.select(select, "containerid"_p = elementId)) {
            cb(row);
        }
        return {};
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

static Expected<std::map<std::string, int>> getDictionary(const std::string& stStr)
{
    std::map<std::string, int> mymap;

    try {
        tnt::Connection db;
        for (const auto& row : db.select(stStr)) {
            mymap.emplace(row.get("name"), row.get<int>("id"));
        }

        return std::move(mymap);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), stStr));
    }
}

Expected<std::map<std::string, int>> readElementTypes()
{
    static const std::string sql = R"(
        SELECT
            v.name, v.id
        FROM
            v_bios_asset_element_type v
    )";

    return getDictionary(sql);
}

Expected<std::map<std::string, int>> readDeviceTypes()
{
    static std::string sql = R"(
        SELECT
            v.name, v.id
        FROM
            v_bios_asset_device_type v
    )";

    return getDictionary(sql);
}

// =====================================================================================================================

Expected<std::vector<DbAssetLink>> selectAssetDeviceLinksTo(uint32_t elementId, uint8_t linkTypeId)
{
    static const std::string sql = R"(
        SELECT
            v.id_asset_element_src, v.src_out, v.dest_in, v.src_name
        FROM
            v_web_asset_link v
        WHERE
            v.id_asset_element_dest = :iddevice AND
            v.id_asset_link_type = :idlinktype
    )";


    try {
        tnt::Connection conn;

        // clang-format off
        auto rows = conn.select(sql,
            "iddevice"_p   = elementId,
            "idlinktype"_p = linkTypeId
        );
        // clang-format on

        std::vector<DbAssetLink> ret;

        for (const auto& row : rows) {
            DbAssetLink link;
            link.destId = elementId;
            row.get("id_asset_element_src", link.srcId);
            row.get("src_out", link.srcSocket);
            row.get("dest_in", link.destSocket);
            row.get("src_name", link.srcName);

            ret.push_back(link);
        }
        return std::move(ret);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<std::map<uint32_t, std::string>> selectShortElements(uint16_t typeId, uint16_t subtypeId)
{
    std::string sql = R"(
        SELECT
            v.name, v.id
        FROM
            v_bios_asset_element v
        WHERE
            v.id_type = :typeid
    )";

    if (subtypeId) {
        sql += "AND v.id_subtype = :subtypeid";
    }

    try {
        tnt::Connection conn;
        auto            st = conn.prepare(sql);
        st.bind("typeid"_p = typeId);
        if (subtypeId) {
            st.bind("subtypeid"_p = subtypeId);
        }

        std::map<uint32_t, std::string> item;

        for (auto const& row : st.select()) {
            item.emplace(row.get<uint32_t>("id"), row.get("name"));
        }

        return std::move(item);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), typeId));
    }
}

// =====================================================================================================================

Expected<int> countKeytag(const std::string& keytag, const std::string& value)
{
    static const std::string sql = R"(
        SELECT COUNT(*) as count
        FROM
            t_bios_asset_ext_attributes
        WHERE
            keytag = :keytag AND
            value = :value
    )";

    try {
        tnt::Connection conn;
        // clang-format off
        return conn.selectRow(sql,
            "keytag"_p = keytag,
            "value"_p  = value
        ).get<int>("count");
        // clang-format on
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), keytag));
    }
}

// =====================================================================================================================

Expected<uint16_t> convertAssetToMonitor(uint32_t assetElementId)
{
    static const std::string sql = R"(
        SELECT
            v.id_discovered_device
        FROM
            v_bios_monitor_asset_relation v
        WHERE
            v.id_asset_element = :id
    )";

    try {
        tnt::Connection conn;
        auto            res = conn.selectRow(sql, "id"_p = assetElementId);
        return res.get<uint16_t>("id_discovered_device");
    } catch (const tntdb::NotFound&) {
        return 0;
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), assetElementId));
    }
}

// =====================================================================================================================

Expected<uint> deleteMonitorAssetRelationByA(tnt::Connection& conn, uint32_t id)
{
    static const std::string sql = R"(
        DELETE FROM
            t_bios_monitor_asset_relation
        WHERE
            id_asset_element = :id
    )";

    try {
        return conn.execute(sql, "id"_p = id);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), id));
    }
}

// =====================================================================================================================

Expected<uint> deleteAssetElement(tnt::Connection& conn, uint32_t elementId)
{
    static const std::string sql = R"(
        DELETE FROM
            t_bios_asset_element
        WHERE
            id_asset_element = :element
    )";

    try {
        return conn.execute(sql, "element"_p = elementId);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<uint> deleteAssetGroupLinks(tnt::Connection& conn, uint32_t assetGroupId)
{
    static const std::string sql = R"(
        DELETE FROM
            t_bios_asset_group_relation
        WHERE
            id_asset_group = :grp
    )";

    try {
        return conn.execute(sql, "grp"_p = assetGroupId);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), assetGroupId));
    }
}

// =====================================================================================================================

Expected<std::vector<uint32_t>> selectAssetsByParent(uint32_t parentId)
{
    static const std::string sql = R"(
        SELECT
            id
        FROM
            v_bios_asset_element
        WHERE id_parent = :parentId
    )";

    try {
        tnt::Connection       conn;
        std::vector<uint32_t> ids;
        for (const auto& it : conn.select(sql, "parentId"_p = parentId)) {
            ids.emplace_back(it.get<uint32_t>("id"));
        }
        return std::move(ids);
    } catch (const tntdb::NotFound&) {
        return unexpected(error(Errors::ElementNotFound).format(parentId));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), parentId));
    }
}

// =====================================================================================================================

Expected<std::vector<uint32_t>> selectAssetDeviceLinksSrc(uint32_t elementId)
{
    static const std::string sql = R"(
        SELECT
            id_asset_device_dest
        FROM
            t_bios_asset_link
        WHERE
            id_asset_device_src = :src
    )";
    try {
        tnt::Connection       conn;
        std::vector<uint32_t> ids;
        for (const auto& it : conn.select(sql, "src"_p = elementId)) {
            ids.emplace_back(it.get<uint32_t>("id_asset_device_dest"));
        }
        return std::move(ids);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), elementId));
    }
}

// =====================================================================================================================

Expected<uint32_t> maxNumberOfPowerLinks()
{
    static const std::string sql = R"(
        SELECT
            MAX(power_src_count) as maxCount
        FROM
            (SELECT COUNT(*) power_src_count FROM t_bios_asset_link
                GROUP BY id_asset_device_dest) pwr
    )";

    try {
        tnt::Connection conn;
        auto            res = conn.selectRow(sql);
        return res.get<uint32_t>("maxCount");
    } catch (const std::exception& e) {
        return unexpected(error(Errors::InternalError).format(e.what()));
    }
}

// =====================================================================================================================

Expected<uint32_t> maxNumberOfAssetGroups()
{
    static const std::string sql = R"(
        SELECT
            MAX(grp_count) as maxCount
        FROM
            ( SELECT COUNT(*) grp_count FROM t_bios_asset_group_relation
                GROUP BY id_asset_element) elmnt_grp
    )";

    try {
        tnt::Connection conn;
        auto            res = conn.selectRow(sql);
        return res.get<uint32_t>("maxCount");
    } catch (const std::exception& e) {
        return unexpected(error(Errors::InternalError).format(e.what()));
    }
}

// =====================================================================================================================

Expected<std::vector<std::string>> selectExtRwAttributesKeytags()
{
    static const std::string sql = R"(
        SELECT
            DISTINCT(keytag)
        FROM
            v_bios_asset_ext_attributes
        WHERE
            read_only = 0
        ORDER BY keytag
    )";

    try {
        tnt::Connection          conn;
        std::vector<std::string> ret;
        for (const auto& row : conn.select(sql)) {
            ret.push_back(row.get("keytag"));
        }
        return std::move(ret);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::InternalError).format(e.what()));
    }
}

// =====================================================================================================================

Expected<std::vector<WebAssetElement>> selectAssetElementAll(const std::optional<uint32_t>& dc)
{
    std::string sql = webAssetSql();
    if (dc) {
        sql += R"(
            WHERE v.id in (
                SELECT p.id_asset_element
                FROM v_bios_asset_element_super_parent p
                WHERE
                    :containerid in ( p.id_asset_element, p.id_parent1, p.id_parent2, p.id_parent3, p.id_parent4,
                        p.id_parent5, p.id_parent6, p.id_parent7, p.id_parent8, p.id_parent9, p.id_parent10)
            )
        )";
    }

    try {
        tnt::Connection              db;
        std::vector<WebAssetElement> list;
        tnt::Rows                    result;
        if (dc) {
            result = db.select(sql, "containerid"_p = *dc);
        } else {
            result = db.select(sql);
        }

        for (const auto& row : db.select(sql)) {
            WebAssetElement& asset = list.emplace_back();
            fetchWebAsset(row, asset);
        }

        return std::move(list);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::InternalError).format(e.what()));
    }
}

// =====================================================================================================================

Expected<std::vector<std::string>> selectGroupNames(uint32_t id)
{
    static const std::string sql = R"(
        SELECT
            v2.name
        FROM v_bios_asset_group_relation v1
        JOIN v_bios_asset_element v2
            ON v1.id_asset_group=v2.id
            WHERE v1.id_asset_element=:id
    )";

    try {
        tnt::Connection conn;

        std::vector<std::string> result;
        for (const auto& row : conn.select(sql, "id"_p = id)) {
            result.push_back(row.get("name"));
        }
        return std::move(result);
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), id));
    }
}

// =====================================================================================================================

Expected<WebAssetElement> findParentByType(uint32_t assetId, uint16_t parentType)
{
    static const std::string sql = R"(
        SELECT
            id_parent,
            id_parent_type
        FROM v_web_element
            WHERE id = :id
    )";

    try {
        tnt::Connection conn;

        uint32_t aid = assetId;
        while (true) {
            auto     row      = conn.selectRow(sql, "id"_p = aid);
            uint32_t idParent = row.get<uint32_t>("id_parent");
            if (idParent) {
                uint16_t type = row.get<uint16_t>("id_parent_type");
                if (type == parentType) {
                    return selectAssetElementWebById(idParent);
                }
                aid = idParent;
            } else {
                break;
            }
        }

        return unexpected(
            error(Errors::ElementNotFound).format("parent with type " + persist::typeid_to_type(parentType)));
    } catch (const std::exception& e) {
        return unexpected(error(Errors::ExceptionForElement).format(e.what(), assetId));
    }
}

// =====================================================================================================================

} // namespace fty::asset::db
