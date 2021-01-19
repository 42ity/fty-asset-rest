#pragma once
#include <fty/expected.h>
#include <fty/translate.h>

namespace fty::asset {

template <typename T>
using AssetExpected = Expected<T, Translate>;

enum class Errors
{
    BadParams,
    InternalError,
    ParamRequired,
    BadRequestDocument,
    ActionForbidden,
    ElementNotFound,
    ExceptionForElement
};

inline Translate error(Errors err)
{
    switch (err) {
    case Errors::BadParams:
        return "Parameter '{}' has bad value. Received {}. Expected {}."_tr;
    case Errors::InternalError:
        return "Internal Server Error. {}."_tr;
    case Errors::ParamRequired:
        return "Parameter '{}' is required."_tr;
    case Errors::BadRequestDocument:
        return "Request document has invalid syntax. {}"_tr;
    case Errors::ActionForbidden:
        return "{} is forbidden. {}"_tr;
    case Errors::ElementNotFound:
        return "Element '{}' not found."_tr;
    case Errors::ExceptionForElement:
        return "exception caught {} for element '{}'"_tr;
    }
    return "Unknown error"_tr;
}

} // namespace fty::asset
