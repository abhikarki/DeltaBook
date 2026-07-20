#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include "orderbook.hpp"

struct ParsedUpdate{
    enum class Type : uint8_t {Snapshot, Delta, Unknown};

    Type type = Type::Unknown;
    uint64_t seq = 0;
    uint64_t local_time_ms = 0;
    uint64_t server_time_ms = 0;

    std::vector<PriceLevel> yes_levels;
    std::vector<PriceLevel> no_levels;

    Side side = Side::Yes;
    int64_t price_ticks = 0;
    int64_t size_delta = 0;
};