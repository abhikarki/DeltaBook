#include "orderbook.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json.hpp>


#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <array>
#include <limits>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

namespace telemetry{
    enum class EventId : size_t{
        JsonParse = 0,
        ParseSnapshotYesLevels,
        ParseSnapshotNoLevels,
        ApplySnapshot,
        DecodeDelta,
        HandleMessageTotal,
        WsRead,
        MessageDispatch,
        Count
    };

    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::nanoseconds;

    struct EventStats{
        uint64_t count = 0;
        int64_t total_ns = 0;
        int64_t min_ns = std::numeric_limits<int64_t>::max();
        int64_t max_ns = 0;
    };

    inline std::array<EventStats, static_cast<size_t>(EventId::Count)> g_stats{};

    inline void record(EventId event, Duration duration){
        auto& stats = g_stats[static_cast<size_t>(event)];
        int64_t ns = duration.count();

        stats.count++;
        stats.total_ns += ns;
        if(ns < stats.min_ns) stats.min_ns = ns;
        if(ns > stats.max_ns) stats.max_ns = ns;
    }

    class ScopedTimer{
        public:
            explicit ScopedTimer(EventId event) : event_(event), start_(Clock::now()){

            }

            ~ScopedTimer(){
                record(event_, Clock::now() - start_);
            }
        private:
            EventId event_;
            Clock::time_point start_;
    };

    inline const char* event_name(EventId e){
        switch(e){
            caseEventId::JsonParse: return "json_parse";
            case EventId::ParseSnapshotYesLevels: return "parse_snapshot_yes_levels";
            case EventId::ParseSnapshotNoLevels: return "parse_snapshot_no_levels";
            case EventId::ApplySnapshot:return "apply_snapshot";
            case EventId::DecodeDelta: return "decode_delta";
            case EventId::ApplyDelta: return "apply_delta";
            case EventId::HandleMessageTotal: return "handle_message_total";
            case EventId::WsRead: return "ws_read";
            case EventId::MessageDispatch:  return "message_dispatch";
            default:
        }
    }

    inline void print_summary(){
        std::cout << std::left << std::setw(30) << "event" << std::right << std::setw(12) << "count" << std::setw(16) << "avg(us)" << std::setw(16) << "min(us)" << std::setw(16) << "max (us)" << "\n";

        for(size_t = 0; i < static<size_t>(EventId::Count); i++){
            const auto& stats = g_stats[i];
            if(stats.count == 0) continue;

            // converting from nanoseconds to microseconds
            double avg = (static_cast<double>(stats.total_ns) / stats.count) / 1000.0;
            double min_us = static_cast<double>(stats.min_ns) / 1000.0;
            double max_us = static_cast<double>(stats.max_ns) / 1000.0;
            std::cout << std::left << std::setw(30) << event_name(static_cast<EventId>(i)) << std::right << std::setw(12) << stats.Count << std::setw(16) << std::setprecision(3) << avg << std::setw(16) << min_us << std::setw(16) << max_us << "\n";
        }
    }

    // to print summary when program terminates, register atexit exactly once 
    inline void install_summary_atexit(){
        static bool installed = false;
        if(!installed){
            std::atexit(print_summary);
            installed = true;
        }
    }
}

namespace config{
    constexpr const char* host = "external-api-ws.demo.kalshi.co";
    constexpr const char* port = "443";
    constexpr const char* ws_path = "/trade-api/ws/v2";

    inline std::string api_key_id(){
        const char* v = std::getenv("KALSHI_API_KEY_ID");
        if(!v) throw std::runtime_error("KALSHI_API_KEY_ID is not set");
        return v;
    }

    inline std::string private_key_path(){
        const char* v = std::getenv("KALSHI_PRIVATE_KEY_PATH");
        if(!v) throw std::runtime_error("KALSHI_PRIVATE_KEY_PATH is not set");
        return v;
    }
}

class KalshiSigner{
    public:
        explicit KalshiSigner(const std::string& key_path){
            FILE* fp = std::fopen(key_path.c_str(), "r");
            if(!fp) throw std::runtime_error("Cannot open private key file: " + key_path);
            key_ = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
            std::fclose(fp);
            if(!key_) throw std::runtime_error("Failed to parse private key: " + last_ssl_error());
        }

        ~KalshiSigner(){
            if(key_) EVP_PKEY_free(key_);
        }

        KalshiSigner(const KalshiSigner&) = delete;
        KalshiSigner& operator=(const KalshiSigner&) = delete;

        // base64(RSA-PSS-SHA256(message)), just as shown in Kalshi docs sample
        std::string sign(const std::string& message) const {
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if(!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");

            EVP_PKEY_CTX* pkey_ctx = nullptr;
            if(EVP_DigestSignInit(ctx, &pkey_ctx, EVP_sha256(), nullptr, key_) <= 0){
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestSignInit failed: " + last_ssl_error());
            }
            if(EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) <= 0){
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("set_rsa_padding_failed: " + last_ssl_error());
            }

            if(EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, RSA_PSS_SALTLEN_DIGEST) <= 0){
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("set_rsa_pss_saltlen failed: " + last_ssl_error());
            }
            if(EVP_DigestSignUpdate(ctx, message.data(), message.size()) <= 0){
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestSignUpdate failed: " + last_ssl_error());
            }
            
            size_t sig_len = 0;
            // calling with nullptr to just query the size
            if(EVP_DigestSignFinal(ctx, nullptr, &sig_len) <= 0){
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestSignFinal (size query) failed: " + last_ssl_error());
            }
            std::vector<unsigned char> sig(sig_len);
            // calling again to get the signature
            if(EVP_DigestSignFinal(ctx, sig.data(), &sig_len) <= 0){
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestSignFinal failed: " + last_ssl_error());
            }
            EVP_MD_CTX_free(ctx);
            return base64_encode(sig.data(), sig_len);
        } 


    private:
        EVP_PKEY* key_ = nullptr;

        // get error code and its readable form
        static std::string last_ssl_error(){
            char buf[256];
            unsigned long code = ERR_get_error();
            ERR_error_string_n(code, buf, sizeof(buf));
            return buf;
        }

        // create base 64 encode filter, attach to memory block, perform base64 encoding and return
        static std::string base64_encode(const unsigned char* data, size_t len){
            BIO* b64 = BIO_new(BIO_f_base64());
            BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
            BIO* mem = BIO_new(BIO_s_mem());
            BIO_push(b64, mem);
            BIO_write(b64, data, static_cast<int>(len));
            BIO_flush(b64);
            BUF_MEM* mem_ptr = nullptr;
            BIO_get_mem_ptr(b64, &mem_ptr);
            std::string result(mem_ptr->data, mem_ptr->length);
            BIO_free_all(b64);
            return result;
        }
};

// time since Unix epoch
std::string current_timestamp_ms(){
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return std::to_string(ms);
}

inline uint64_t json_to_u64(const json::value& v){
    if(v.is_int64()) return static_cast<uint64_t>(v.as_int64());
    if(v.is_uint64()) return v.as_uint64();
    if(v.is_double()) return static_cast<uint64_t>(v.as_double());
    return 0;
}

inline Side parse_side(const std::string& s){
    return (s == "yes") ? Side::Yes : Side::No;
}

inline std::vector<PriceLevel> parse_levels(const json::array& arr){
    std::vector<PriceLevel> levels;
    levels.reserve(arr.size());
    for(auto const& entry : arr){
        auto const& pair = entry.as_array();
        levels.push_back({parse_price(std::string(pair[0].as_string())), parse_size(std::string(pair[1].as_string()))});
    }
    return levels;
}

void handle_message(const std::string& raw, const std::shared_ptr<SharedOrderBook>& book, bool print_updates){
    // to measure the total duration for handle_message
    telemetry::ScopedTimer total_timer(telemetry::EventId::HandleMessageTotal);
    
    json::value parsed;
    {
        telemetry::ScopedTimer json_timer(telemetry::EventId::JsonParse);
        parsed = json::parse(raw);
    } 
    json::object& obj = parsed.as_object();

    std::string type;
    if(auto* t = obj.if_contains("type"); t && t->is_string()) type = std::string(t->as_string());

    uint64_t seq = 0;
    if(auto* s = obj.if_contains("seq")) seq = json_to_u64(*s);

    auto* msg_ptr = obj.if_contains("msg");
    if(!msg_ptr || !msg_ptr->is_object()){
        if(type == "error" && print_updates) std::cerr << "[error]" << raw << "\n";
        return;
    }
    json::object& msg = msg_ptr->as_object();

    telemetry::ScopedTimer dispatch_timer(telemetry::EventId::MessageDispatch);

    if(type == "orderbook_snapshot"){
        std::vector<PriceLevel> yes_levels, no_levels;

        if(auto* y = msg.if_contains("yes_dollars_fp"); y && y->is_array()){
            telemetry::ScopedTimer snap_yes_timer(telemetry::EventId::ParseSnapshotYesLevels);
            yes_levels = parse_levels(y->as_array());
        }
        if(auto* n = msg.if_contains("no_dollars_fp"); n && n->is_array()){
            telemetry::ScopedTimer snap_no_timer(telemetry::EventId::ParseSnapshotNoLevels);
            no_levels = parse_levels(n->as_array());
        }

        {
            telemetry::ScopedTimer apply_snap_timer(telemetry::EventId::ApplySnapshot);
            book->load_snapshot(yes_levels, no_levels, seq);
        }
       

        if(print_updates){
            std::cout << "[snapshot] seq= " << seq << " yes_levels= " << yes_levels.size() << " no_levels= " << no_levels.size() << "\n";
        }
    }
    else if(type == "orderbook_delta"){
        OrderBookDelta d;

        {
            telemetry::ScopedTimer decode_delta_timer(telemetry::EventId::DecodeDelta);
            d.side = parse_side(std::string(msg["side"].as_string()));
            d.price_ticks = parse_price(std::string(msg["price_dollars"].as_string()));
            d.size_delta = parse_size(std::string(msg["delta_fp"].as_string()));
            d.seq = seq;
        }

        {
            telemetry::ScopedTimer apply_delta_timer(telemetry::EventId::ApplyDelta);
            book->update(d);
        }
        

        if(print_updates){
            BookTop top = book->read_snapshot();
            std::cout << "[delta] seq= " << seq << " yes_bid= " << top.yes_bid << " yes_ask= " << top.yes_ask   
                      << " no_bid=" << top.no_bid << "no_ask =  " << top.no_ask << (top.is_synced ? "" : " some gaps in delta stream ") << "\n";
        }
    }
}

void run_kalshi_feed(std::shared_ptr<SharedOrderBook> book, std::string market_ticker, bool print_updates){
    try{
        // as per the Kalshi API Docs
        KalshiSigner signer(config::private_key_path());
        const std::string timestamp = current_timestamp_ms();
        const std::string msg_to_sign = timestamp + "GET" + config::ws_path;
        const std::string signature = signer.sign(msg_to_sign);

        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);

        tcp::resolver resolver{ioc};
        // create the network stack
        websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{ioc, ctx};

        auto const results = resolver.resolve(config::host, config::port);

        // TCP connection
        beast::get_lowest_layer(ws).connect(results);

        // set SNI
        if(!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), config::host)){
            throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()));
        }
        // TLS/SSL handshake
        ws.next_layer().handshake(ssl::stream_base::client);

        // update the request
        ws.set_option(websocket::stream_base::decorator(
            [&](websocket::request_type& req){
                req.set("KALSHI-ACCESS-KEY", config::api_key_id());
                req.set("KALSHI-ACCESS-SIGNATURE", signature);
                req.set("KALSHI-ACCESS-TIMESTAMP", timestamp);
            }
        ));

        const std::string host_header = std::string(config::host) + ":" + config::port;
        ws.handshake(host_header, config::ws_path);

        std::cout << "connected to " << config::host << "\n";

        json::object sub;
        sub["id"] = 1;
        sub["cmd"] = "subscribe";
        json::object params;
        params["channels"] = json::array{"orderbook_delta"};
        params["market_tickers"] = json::array{market_ticker};
        sub["params"] = params;

        ws.write(net::buffer(json::serialize(sub)));

        beast::flat_buffer buffer;
        for(;;){
            buffer.clear();
            ws.read(buffer);
            handle_message(beast::buffers_to_string(buffer.data()), book, print_updates);
        }
    } catch(std::exception const& e){
        std::cerr << "Error: " << e.what() << "\n";
    }
}

#ifndef KALSHI_PYBIND_BUILD
int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "missing command line arguments" << std::endl;
        return 1;
    }

    std::string market_ticker = argv[1];

    auto book = std::make_shared<SharedOrderBook>();
    run_kalshi_feed(book, market_ticker, true);

    return 0;
}
#endif