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
    std::string extName;
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
/// @return asset is or error
Expected<int64_t> nameToAssetId(const std::string& assetName); //!test

/// Converts database id to internal name and extended (unicode) name
/// @param assetId asset id
/// @return pair of name and extended name or error
Expected<std::pair<std::string, std::string>> idToNameExtName(uint32_t assetId); //!test

/// Converts asset's extended name to its internal name
/// @param assetExtName asset external name
/// @return internal name or error
Expected<std::string> extNameToAssetName(const std::string& assetExtName); //!test

/// Converts internal name to extended name
/// @param assetName asset internal name
/// @return external name or error
Expected<std::string> nameToExtName(std::string assetName);

/// Converts asset's extended name to id
/// @param assetExtName asset external name
/// @return id or error
Expected<int64_t> extNameToAssetId(const std::string& assetExtName); //!test

/// select basic information about asset element by name
/// @param name asset internal or external name
/// @param extNameOnly select by external name only
/// @return AssetElement or error
Expected<AssetElement> selectAssetElementByName(const std::string& name, bool extNameOnly = false);  //!test

/// Selects all data about asset in WebAssetElement
/// @param elementId asset element id
/// @return WebAssetElement or error
Expected<WebAssetElement> selectAssetElementWebById(uint32_t elementId); //!test

/// Selects all data about asset in WebAssetElement
/// @param elementId asset element id
/// @param asset WebAssetElement to select to
/// @return nothing or error
Expected<void> selectAssetElementWebById(uint32_t elementId, WebAssetElement& asset); //!test

/// Selects all ext_attributes of asset
/// @param elementId asset element id
/// @return Attributes map or error
Expected<Attributes> selectExtAttributes(uint32_t elementId); //!test

/// get information about the groups element belongs to
/// @param elementId element id
/// @return groups map or error
Expected<std::map<uint32_t, std::string>> selectAssetElementGroups(uint32_t elementId); //!test


/// Updates asset element
/// @param conn database established connection
/// @param elementId element id
/// @param parentId parent id
/// @param status status string
/// @param priority priority
/// @param assetTag asset tag
/// @return affected rows count or error
Expected<uint> updateAssetElement(tnt::Connection& conn, uint32_t elementId, uint32_t parentId,
    const std::string& status, uint16_t priority, const std::string& assetTag); //!test

/// Deletes all ext attributes of given asset with given 'read_only' status
/// @param conn database established connection
/// @param elementId asset element id
/// @param readOnly read only flag
/// @return deleted rows or error
Expected<uint> deleteAssetExtAttributesWithRo(tnt::Connection& conn, uint32_t elementId, bool readOnly); //!test

/// Insert given ext attributes map into t_bios_asset_ext_attributes
/// @param conn database established connection
/// @param attributes attributes map - {key, value}
/// @param readOnly 'read_only' status
/// @return affected rows count or error
Expected<uint> insertIntoAssetExtAttributes(
    tnt::Connection& conn, uint32_t elementId, const std::map<std::string, std::string>& attributes, bool readOnly); //!test

/// Delete asset from all group
/// @param conn database established connection
/// @param elementId asset element id to delete
/// @return count of affected rows or error
Expected<uint> deleteAssetElementFromAssetGroups(tnt::Connection& conn, uint32_t elementId); //!test

/// Inserts info about an asset
/// @param conn database established connection
/// @param element element to insert
/// @param update update or insert flag
/// @return count of affected rows or error
Expected<uint32_t> insertIntoAssetElement(tnt::Connection& conn, const AssetElement& element, bool update); //!test

/// Inserts asset into groups
/// @param conn database established connection
/// @param groups groups id to insert
/// @param elementId element id
/// @return count of affected rows or error
Expected<uint> insertElementIntoGroups(tnt::Connection& conn, const std::set<uint32_t>& groups, uint32_t elementId); //!test

/// Deletes all links which have given asset as 'dest'
/// @param conn database established connection
/// @param elementId element id
/// @return count of affected rows or error
Expected<uint> deleteAssetLinksTo(tnt::Connection& conn, uint32_t elementId); //!test

/// Inserts powerlink info
/// @param conn database established connection
/// @param link powerlink info
/// @return inserted id or error
Expected<int64_t> insertIntoAssetLink(tnt::Connection& conn, const AssetLink& link); //!test

/// Inserts powerlink infos
/// @param conn database established connection
/// @param links list of powerlink info
/// @return count of inserted links or error
Expected<uint> insertIntoAssetLinks(tnt::Connection& conn, const std::vector<AssetLink>& links); //! test

/// Inserts name<->device_type relation
/// @param conn database established connection
/// @param deviceTypeId device type
/// @param deviceName device name
/// @return new relation id or error
Expected<uint16_t> insertIntoMonitorDevice(tnt::Connection& conn, uint16_t deviceTypeId, const std::string& deviceName);  //! test

/// Inserts monitor_id<->element_id relation
/// @param conn database established connection
/// @param monitorId monitor id
/// @param elementId element id
/// @return new relation id or error
Expected<int64_t> insertIntoMonitorAssetRelation(tnt::Connection& conn, uint16_t monitorId, uint32_t elementId); //! test

/// Selects id based on name from v_bios_device_type
/// @param conn database established connection
/// @param deviceTypeName device type name
/// @return device type id or error
Expected<uint16_t> selectMonitorDeviceTypeId(tnt::Connection& conn, const std::string& deviceTypeName); //! test

/// Selects parents of given device
/// @param id asset element id
/// @param cb callback function
/// @return nothing or error
Expected<void> selectAssetElementSuperParent(uint32_t id, SelectCallback&& cb); //!test

/// Selects assets from given container (DB, room, rack, ...) without - accepted values: "location", "powerchain" or
/// empty string
/// @param conn database established connection
/// @param elementId asset element id
/// @param types asset types to select
/// @param subtypes asset subtypes to select
/// @param status asset status
/// @param cb callback function
/// @return nothing or error
Expected<void> selectAssetsByContainer(tnt::Connection& conn, uint32_t elementId, std::vector<uint16_t> types,
    std::vector<uint16_t> subtypes, const std::string& without, const std::string& status, SelectCallback&& cb);

/// Gets data about the links the specified device belongs to
/// @param elementId element id
/// @param linkTypeId link type id
/// @return list of links or error
Expected<std::vector<DbAssetLink>> selectAssetDeviceLinksTo(uint32_t elementId, uint8_t linkTypeId); //! test

/// Selects all devices of certain type/subtype
/// @param typeId type id
/// @param subtypeId subtype id
/// @return map of devices or error
Expected<std::map<uint32_t, std::string>> selectShortElements(uint16_t typeId, uint16_t subtypeId); //! test

/// Returns how many times is gived a couple keytag/value in t_bios_asset_ext_attributes
/// @param keytag keytag
/// @param value asset name
/// @return count or error
Expected<int> countKeytag(const std::string& keytag, const std::string& value); //! test

/// Converts asset id to monitor id
/// @param assetElementId asset element id
/// @return monitor id or error
Expected<uint16_t> convertAssetToMonitor(uint32_t assetElementId);

/// Deletes given asset from t_bios_monitor_asset_relation
/// @param conn database established connection
/// @param id asset element id
/// @return count of affected rows or error
Expected<uint> deleteMonitorAssetRelationByA(tnt::Connection& conn, uint32_t id); //! test

/// Deletes asset from t_bios_asset_element
/// @param conn database established connection
/// @param elementId asset element id
/// @return count of affected rows or error
Expected<uint> deleteAssetElement(tnt::Connection& conn, uint32_t elementId); //! test

/// Deletes all data about a group
/// @param conn database established connection
/// @param assetGroupId asset group id
/// @return count of affected rows or error
Expected<uint> deleteAssetGroupLinks(tnt::Connection& conn, uint32_t assetGroupId); //! test

/// Selects childrent element id
/// @param parentId parent id
/// @return list of children or error
Expected<std::vector<uint32_t>> selectAssetsByParent(uint32_t parentId);

/// Selects all corresponding links for element
/// @param elementId element id
/// @return list of devices where element is linked or error
Expected<std::vector<uint32_t>> selectAssetDeviceLinksSrc(uint32_t elementId); //! test

Expected<std::map<std::string, int>> readElementTypes();
Expected<std::map<std::string, int>> readDeviceTypes();

/// Selects maximum number of power sources for device in the system
/// @return  number of power sources or error
Expected<uint32_t> maxNumberOfPowerLinks();

/// Selects maximal number of groups in the system
/// @return number of groups or error
Expected<uint32_t> maxNumberOfAssetGroups();

/// Selects all read-write ext attributes
/// @return attribures names or error
Expected<std::vector<std::string>> selectExtRwAttributesKeytags();

/// Selects everything from v_web_element
/// @param dc datacenter id, if is set then returns assets for datacenter only
/// @return list of elements or error
Expected<std::vector<WebAssetElement>> selectAssetElementAll(const std::optional<uint32_t>& dc = std::nullopt);

/// Selects all group names for given element id
/// @param id asset element id
/// @return group names or error
Expected<std::vector<std::string>> selectGroupNames(uint32_t id);

/// Finds parent by type for asset
/// @param assetId asset id
/// @param parentType parent type
Expected<WebAssetElement> findParentByType(uint32_t assetId, uint16_t parentType);
} // namespace fty::asset::db
