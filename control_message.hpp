#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

struct ControlMessage{
    enum class Type{
        Subscribed,
        Ok,
        Unsubscribed,
        Error,
        Other
    };

    Type type = Type::Other;
    
    int64_t id = 0;
    int64_t sid = -1;
    uint64_t seq = 0;
    std::string channel;
    std::vector<std::string> market_tickers;

    int error_code = 0;
    std::string error_msg;
};

