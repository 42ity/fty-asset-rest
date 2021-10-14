#pragma once
#include <list>
#include <pack/pack.h>
#include <string>

// =====================================================================================================================

namespace messagebus {
class Message;
}

// =====================================================================================================================

namespace fty::asset {

/// Common message bus message temporary wrapper
class Message : public pack::Node
{
public:
    enum class Status
    {
        Ok,
        Error
    };

    struct Meta : public pack::Node
    {
        pack::String         replyTo       = FIELD("reply-to");
        mutable pack::String from          = FIELD("from");
        mutable pack::String to            = FIELD("to");
        pack::String         subject       = FIELD("subject");
        pack::Enum<Status>   status        = FIELD("status");
        pack::String         timeout       = FIELD("timeout");
        mutable pack::String correlationId = FIELD("correlation-id");

        using pack::Node::Node;
        META(Meta, replyTo, from, to, subject, status, timeout, correlationId);
    };

    using Data = pack::StringList;

public:
    Meta meta     = FIELD("meta-data");
    Data userData = FIELD("user-data");

public:
    using pack::Node::Node;
    META(Message, userData, meta);

public:
    explicit Message(const messagebus::Message& msg);
    messagebus::Message toMessageBus() const;
    void                setData(const std::string& data);
    void                setData(const std::list<std::string>& data);
};

inline std::ostream& operator<<(std::ostream& ss, Message::Status status)
{
    switch (status) {
        case Message::Status::Ok:
            ss << "ok";
            break;
        case Message::Status::Error:
            ss << "ko";
            break;
    }
    return ss;
}

inline std::istream& operator>>(std::istream& ss, Message::Status& status)
{
    std::string str;
    ss >> str;
    if (str == "ok") {
        status = Message::Status::Ok;
    } else if (str == "ko") {
        status = Message::Status::Error;
    }
    return ss;
}

} // namespace fty::asset

// =====================================================================================================================
