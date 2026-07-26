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


// check the response to see what type of message it is
inline std::optional<ControlMessage> parse_control(const std::string& raw, simdjson::ondemand::parser& parser){
    simdjson:padded_string doc_buf(raw);

    simdjson::ondemand::document doc;
    if(parser.iterate(doc_buf).get(doc) != simdjson::SUCCESS) return std::nullopt;

    std::string_view type_sv;
    if(doc["type"].get_string().get(type_sv) != simdjson::SUCCESS) return std::nullopt;

    ControlMessage cm;

    int64_t id_val = 0;
    if(doc["id"].get(id_val) == simdjson::SUCCESS) cm.id = id_val;

    if(type_sv == "subscribed"){
        cm.type = ControlMessage::Type::Subscribed;
        simdjson::ondemand::object msg;
        if(doc["msg"].get_object().get(msg) == simdjson::SUCCESS){
            std::string_view chan_sv;
            if(msg["channel"].get_string().get(chan_sv) == simdjson::SUCCESS) cm.channel = std::string(chan_sv);
            int64_t sid_val;
            if(msg["sid"].get(sid_val) == simdjson::SUCCESS) cm.sid = sid_val;
        }
    }
    else if(type_sv == "ok"){
        cm.type = ControlMessage::Type::Ok;
        int64_t sid_val;
        if(doc["sid"].get(sid_val) == simdjson::SUCCESS) cm.sid = sid_val;
        uint64_t seq_val;
        if(doc["seq"].get(seq_val) == simdjson::SUCCESS) cm.seq = seq_val;

        simdjson::ondemand::value msg_val;
        if(doc["mgs"].get(msg_val) == simdjson::SUCCESS){
            simdjson::ondemand::object msg;
            if(msg_val.get_object().get(msg) == simdjson::SUCCESS){
                simdjson::ondemand::array tickers;
                if(msg["market_tickers"].get_array().get(tickers) == simdjson::SUCCESS){
                    for(auto t : tickers){
                        std::string_view tsv;
                        if(t.get_string().get(tsv) == simdjson::SUCCESS) cm.market_tickers.push_back(std::string(tsv));
                    }
                }
            }
        }
    }
    else if(type_sv == "unsubscribed"){
        cm.type = ControlMessag::Type::Unsubscribed;
        int64_t sid_val;
        if(doc["sid"].get(sid_val) == simdjson::SUCCESS) cm.sid = sid_val;
        uint64_t seq_val;
        if(doc["seq"].get(seq_val) == simdjson::SUCCESS) cm.seq = seq_val;
    }
    else if(type_sv == "error"){
        cm.type = ControlMessage::Type::Error;
        simdjson::ondemand::object msg;
        if(doc["msg"].get_object().get(msg) == simdjson::SUCCESS){
            int64_t code_val;
            if(msg["code"].get(code_val) == simdjson::SUCCESS) cm.error_code = static_cast<int>(code_val);
            std::string_view msg_sv;
            if(msg["msg"].get_string().get(msg_sv) == simdjson::SUCCESS) cm.error_msg = std::string(msg_sv);
        }
    }
    else{
        // so most likely market data
        return std::nullopt;
    }

}