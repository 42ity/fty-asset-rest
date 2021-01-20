#pragma once
#include "error.h"
#include <fty/expected.h>
#include <string>


namespace fty::asset {

AssetExpected<uint32_t>    checkElementIdentifier(const std::string& paramName, const std::string& paramValue);
AssetExpected<std::string> sanitizeDate(const std::string& inp);
AssetExpected<double>      sanitizeValueDouble(const std::string& key, const std::string& value);
AssetExpected<void> tryToPlaceAsset(uint32_t id, uint32_t parentId, uint32_t size, uint32_t loc);

} // namespace fty::asset
