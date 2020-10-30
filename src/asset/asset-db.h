#pragma once
#include <fty/expected.h>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace tnt {
class Connection;
class Row;
} // namespace tnt

namespace fty::asset::db {

// =====================================================================================================================

struct AssetElement
{
public:
    uint32_t                           id = 0;
    std::string                        name;
    std::string                        status;
    uint32_t                           parentId  = 0;
    uint16_t                           priority  = 0;
    uint16_t                           typeId    = 0;
    uint16_t                           subtypeId = 0;
    std::string                        assetTag;
    std::map<std::string, std::string> ext;
};

struct WebAssetElement : public AssetElement
{
    std::string typeName;
    uint16_t    parentTypeId;
    std::string subtypeName;
    std::string parentName;
};

struct DbAssetLink
{
    uint32_t    srcId;
    uint32_t    destId;
    std::string srcName;
    std::string srcSocket;
    std::string destSocket;
};

struct AssetLink
{
    uint32_t    src;    //!< id of src element
    uint32_t    dest;   //!< id of dest element
    std::string srcOut; //!< outlet in src
    std::string destIn; //!< inlet in dest
    uint16_t    type;   //!< link type id
};

struct ExtAttrValue
{
    std::string value;
    bool        readOnly = false;
};

using Attributes = std::map<std::string, ExtAttrValue>;

struct WebAssetElementExt : public WebAssetElement
{
    std::map<uint32_t, std::string>                                          groups;
    std::vector<DbAssetLink>                                                 powers;
    Attributes                                                               extAttributes;
    std::vector<std::tuple<uint32_t, std::string, std::string, std::string>> parents; // list of parents (id, name)
};

using SelectCallback = std::function<void(const tnt::Row&)>;

// =====================================================================================================================

/// Converts asset internal name to database id
/// @param assetName internal name of the asset
/// @returns asset is or error
Expected<int64_t> nameToAssetId(const std::string& assetName);

/// Converts database id to internal name and extended (unicode) name
/// @param assetId asset id
/// returns pair of name and extended name or error
Expected<std::pair<std::string, std::string>> idToNameExtName(uint32_t assetId);

/// Converts asset's extended name to its internal name
/// @param assetExtName asset external name
/// returns internal name or error
Expected<std::string> extNameToAssetName(const std::string& assetExtName);

/// Converts asset's extended name to id
/// @param assetExtName asset external name
/// returns id or error
Expected<int64_t> extNameToAssetId(const std::string& assetExtName);

/// select basic information about asset element by name
/// @param name asset internal or external name
/// return @ref AssetElement or error
Expected<AssetElement> selectAssetElementByName(const std::string& name);

/// Selects all data about asset in WebAssetElement
/// @param elementId asset element id
/// @returns @ref WebAssetElement or error
Expected<WebAssetElement> selectAssetElementWebById(uint32_t elementId);

/// Selects all data about asset in WebAssetElement
/// @param elementId asset element id
/// @param asset @ref WebAssetElement to select to
/// @returns nothing or error
Expected<void> selectAssetElementWebById(uint32_t elementId, WebAssetElement& asset);

/// Selects all ext_attributes of asset
/// @param elementId asset element id
/// @return Attributes map or error
Expected<Attributes> selectExtAttributes(uint32_t elementId);

/// get information about the groups element belongs to
/// @param elementId element id
/// returns groups map or error
Expected<std::map<uint32_t, std::string>> selectAssetElementGroups(uint32_t elementId);


/// Updates asset element
/// @param conn database established connection
/// @param elementId element id
/// @param parentId parent id
/// @param status status string
/// @param priority priority
/// @param assetTag asset tag
/// @returns affected rows count or error
Expected<uint> updateAssetElement(tnt::Connection& conn, uint32_t elementId, uint32_t parentId,
    const std::string& status, uint16_t priority, const std::string& assetTag);

/// Deletes all ext attributes of given asset with given 'read_only' status
/// @param conn database established connection
/// @param elementId asset element id
/// @param readOnly read only flag
/// @returns deleted rows or error
Expected<uint> deleteAssetExtAttributesWithRo(tnt::Connection& conn, uint32_t elementId, bool readOnly);

/// Insert given ext attributes map into t_bios_asset_ext_attributes
/// @param conn database established connection
/// @param attributes attributes map - {key, value}
/// @param readOnly 'read_only' status
Expected<uint> insertIntoAssetExtAttributes(
    tnt::Connection& conn, uint32_t elementId, const std::map<std::string, std::string>& attributes, bool readOnly);

/// Delete asset from all group
/// @param conn database established connection
/// @param elementId asset element id to delete
/// @returns count of affected rows or error
Expected<uint> deleteAssetElementFromAssetGroups(tnt::Connection& conn, uint32_t elementId);

/// Inserts info about an asset
/// @param conn database established connection
/// @param element element to insert
/// @param update update or insert flag
/// @returns count of affected rows or error
Expected<int64_t> insertIntoAssetElement(tnt::Connection& conn, const AssetElement& element, bool update);

/// Inserts asset into groups
/// @param conn database established connection
/// @param groups groups id to insert
/// @param elementId element id
/// @returns count of affected rows or error
Expected<uint> insertElementIntoGroups(tnt::Connection& conn, const std::set<uint32_t>& groups, uint32_t elementId);

/// Deletes all links which have given asset as 'dest'
/// @param conn database established connection
/// @param elementId element id
/// @returns count of affected rows or error
Expected<uint> deleteAssetLinksTo(tnt::Connection& conn, uint32_t elementId);

/// Inserts powerlink info
/// @param conn database established connection
/// @param link powerlink info
/// @returns inserted id or error
Expected<int64_t> insertIntoAssetLink(tnt::Connection& conn, const AssetLink& link);

/// Inserts powerlink infos
/// @param conn database established connection
/// @param links list of powerlink info
/// @returns count of inserted links or error
Expected<uint> insertIntoAssetLinks(tnt::Connection& conn, const std::vector<AssetLink>& links);

/// Inserts name<->device_type relation
/// @param conn database established connection
/// @param deviceTypeId device type
/// @param deviceName device name
/// @returns new relation id or error
Expected<uint16_t> insertIntoMonitorDevice(tnt::Connection& conn, uint16_t deviceTypeId, const std::string& deviceName);

/// Inserts monitor_id<->element_id relation
/// @param conn database established connection
/// @param monitorId monitor id
/// @param elementId element id
/// @returns new relation id or error
Expected<int64_t> insertIntoMonitorAssetRelation(tnt::Connection& conn, uint16_t monitorId, uint32_t elementId);

/// Selects id based on name from v_bios_device_type
/// @param conn database established connection
/// @param deviceTypeName device type name
/// @returns device type id or error
Expected<uint16_t> selectMonitorDeviceTypeId(tnt::Connection& conn, const std::string& deviceTypeName);

/// Selects parents of given device
/// @param id asset element id
/// @param cb callback function
/// @returns nothing or error
Expected<void> selectAssetElementSuperParent(uint32_t id, SelectCallback&& cb);

/// Selects assets from given container (DB, room, rack, ...) without - accepted values: "location", "powerchain" or
/// empty string
/// @param conn database established connection
/// @param elementId asset element id
/// @param types asset types to select
/// @param subtypes asset subtypes to select
/// @param status asset status
/// @param cb callback function
/// @returns nothing or error
Expected<void> selectAssetsByContainer(tnt::Connection& conn, uint32_t elementId, std::vector<uint16_t> types,
    std::vector<uint16_t> subtypes, const std::string& without, const std::string& status, SelectCallback&& cb);

/// Gets data about the links the specified device belongs to
/// @param elementId element id
/// @param linkTypeId link type id
/// @returns list of links or error
Expected<std::vector<DbAssetLink>> selectAssetDeviceLinksTo(uint32_t elementId, uint8_t linkTypeId);

/// Selects all devices of certain type/subtype
/// @param typeId type id
/// @param subtypeId subtype id
/// @returns map of devices or error
Expected<std::map<uint32_t, std::string>> selectShortElements(uint16_t typeId, uint16_t subtypeId);

/// Returns how many times is gived a couple keytag/value in t_bios_asset_ext_attributes
/// @param keytag keytag
/// @param value asset name
/// @returns count or error
Expected<int> countKeytag(const std::string& keytag, const std::string& value);

Expected<std::map<std::string, int>> readElementTypes();
Expected<std::map<std::string, int>> readDeviceTypes();

} // namespace fty::asset::db
