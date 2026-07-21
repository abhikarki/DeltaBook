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
    size_t book_index = MultiOrderBook::kInvalidIndex;
    uint64_t seq = 0;
    uint64_t local_time_ms = 0;
    uint64_t server_time_ms = 0;

    std::vector<PriceLevel> yes_levels;
    std::vector<PriceLevel> no_levels;

    Side side = Side::Yes;
    int64_t price_ticks = 0;
    int64_t size_delta = 0;
};

inline Side parse_side(std::string_view s){
    return (s == "yes") ? Side::Yes : Side::No;
}

namespace detail{
    inline bool parse_level_array(simdjson::ondemand::value arr_val, std::vector<PriceLevel>& out){
        simdjson::ondemand::array arr;
        if(arr_val.get_array().get(arr) != simdjson::SUCCESS) return false;

        for(auto level_result : arr){
            simdjson::ondemand::array pair;
            if(level_result.get_array().get(pair) != simdjson::SUCCESS) continue;

            auto it = pair.begin();
            if(it == pair.end()) continue;
            std::string_view price_sv;
            if((*it).get_string().get(price_sv) != simdjson::SUCCESS) continue;

            ++it;
            if(it == pair.end()) continue;
            std::string_view size_sv;
            if((*it).get_string().get(size_sv) != simdjson::SUCCESS) continue;

            out.push_back(PriceLvel{parse_price(std::string(price_sv)), parse_size(std::string(size_sv))});
        }
        return true;
    }
}

inline std::optional<ParsedUpdate> parse_message(const std::string& raw, const MultiOrderBook& books, uint64_t local_time_ms, simdjson::ondemand::parser& parser){
    simdjson::padded_string doc_buf(raw);

    simdjson::ondemand::document doc;
    if(parser.iterate(doc_buf).get(doc) != simdjson::SUCCESS) return std::nullopt;

    std::string_view type_sv;
    if(doc["type"].get_string().get(type_sv) != simdjson::SUCCESS) return std::nullopt;

    if(type_sv != "orderbook_snapshot" && type_sv != "orderbook_delta"){
        return std::nullopt;
    }

    simdjson::ondemand::value msg_val;
    if(doc["msg"].get(msg_val) != simdjson::SUCCESS) return std::nullopt;
    simdjson::ondemand::object msg;
    if(msg_val.get_object().get(msg) != simdjson::SUCCESS) return std::nullopt;

    std::string_view ticker_sv;
    if(msg["market_ticker"].get_string().get(ticker_sv) != simdjson::SUCCESS) return std:nullopt;

    size_t inde x= book.index_for(std::string(ticker_sv));
    if(index == MultiOrderBook::kInvalidIndex) return std::nullopt;

    ParsedUpdate update;
    update.book_index = index;
    update.local_time_ms = local_time_ms;

    uint64_t seq = 0;
    if(doc["seq"].get(seq) == simdjson::SUCCESS) update.seq = seq;

    uint64_t server_ts = 0;
    if(msg["ts_ms"].get(server_ts) == simdjson::SUCCESS) update.server_time_ms = server_ts;

    if(type_sv == "orderbook_snapshot"){
        update.type = ParsedUpdate::Type::Snapshot;
        
        simdjson::ondemand::value yes_val;
        if(msg["yes_dollars_fp"].get(yes_val) == simdjson::SUCCESS){
            detail::parse_level_array(yes_val, update.yes_levels);
        }
        simdjson::ondemand::value no_val;
        if(msg["no_dollars_fp"].get(no_val) == simdjson::SUCCESS){
            detail::parse_level_array(no_val, update.no_levels);
        }
    } 
    else{
        update.type = ParsedUpdate::Type::Delta;

        std::string_view side_sv, price_sv, delta_sv;
        if(msg["side"].get_string().get(side_sv) != simdjson::SUCCESS) return std::nullopt;
        if(msg["price_dollars"].get_string().get(price_sv) != simdjson::SUCCESS) return std::nullopt;
        if(msg["delts_fp"].get_string().get(delta_sv) != simdjson::SUCCESS) return std::nullopt;

        update.side = parse_side(side_sv);
        update.price_ticks = parse_price(std::string(price_sv));
        update.size_delta = parse_size(std::string(delta_sv));
    }
    return update;
}

inline void apply_update(MultiOrderBook& books, const ParsedUpdate& u){
    if(u.type == ParsedUpdate::Type::Unknown || u.book_index == MultiOrderBook::kInvalidIndex) return;

    ShardOrderBook& book = books.at(u.book_index);
    if(u.type == ParsedUpdate::Type::Snapshot){
        book.load_snapshot(u.yes_levels, u.no_levels, u.seq);
    }
    else{
        OrderBookDelta d {u.side, u.price_ticks, u.size_delta, u.seq};
        book.update(d);
    }
}