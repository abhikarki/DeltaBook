#include "orderbook.hpp"
#include "telemetry.hpp"
#include "spsc_queue.hpp"
#include "parsed_update.hpp"
#include "control_message.hpp"
#include "command_queue.hpp"

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
#include <csignal>
#include <atomic>
#include <thread>
#include <optional>
#include <unordered_map>
#include <deque>


// global boolean to stop the infinite loop reading data
std::atomic<bool> g_running{true};

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

// to modify later for core isolation optimization
namespace feed_topology{
    inline unsigned int available_hardware_concurrency(){
        unsigned int n = std::thread::hardware_concurrency();
        return n == 0 ? 1 : n;
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

constexpr size_t kQueueCapacity = 4096;

// This class handles both read and write for the kalshi websocket.
// 
class KalshiConnection{
    public:
        KalshiConnection(net::io_context& ioc, ssl::context& ssl_ctx, std::shared_ptr<MultiOrderBook> books, SPSCQueue<ParsedUpdate>& out_queue, CommandQueue& commands, bool print_updates)
            : ioc_(ioc), ws_(ioc, ssl_ctx), books_(std::move(books)), out_queue_(out_queue), commands_(commands), print_updates_(print_updates)
        {

        }

        // initial connection
        void connect_and_handshake(const std::string& api_key_id, const std::string& signature, const std::string& timestamp){
            tcp::resolver resolver{ioc_};
            auto const results = resolver.resolve(config::host, config::port);

            beast::get_lowest_layers(ws_).connect(results);
            if(!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), config::host)){
                throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()));
            }
            ws_.next_layer().handshake(ssl::stream_base::client);

            ws_.set_option(websocket::stream_base::decorator(
                [api_key_id, signature, timestamp](websocket::request_type& req){
                    req.set("KALSHI-ACCESS-KEY", api_key_id);
                    req.set("KALSHI-ACCESS-SIGNATURE", signature);
                    req.set("KALSHI-ACCESS-TIMESTAMP", timestamp);
                }
            ));

            const std::string host_header = std::string(config::host) + ":" + config::port;
            ws_handshake(host_header, config::ws_path);

            std::cout << "Connected to " << config::host << "\n";
        }

        // the reading thread starts here by subscribing to the initial tickers. we are following the same procedure of creating commands and 
        // queue to send subscription message just as we follow for adding tickers in live execution.
        void start(const std::vector<std::string>& initial_tickers){
            for(auto const& ticker : initial_tickers) commands_.push(Command({CommandType::AddTicker, ticker}));
            drain_commands();
            do_read();
        }

        void notify_commands(){
            net::post(ioc_, [this]() {
                drain_commands();
            });
        }

        void stop(){
            beast::error_code ec;
            ws_.close(websocket::close_code::normal, ec);
        }
    
    private:
        // after the read completes, the callback calls on_read
        void do_read(){
            // clear the buffer for next read
            buffer_.consume(buffer_.size());
            ws_.async_read(buffer_, [this](beast::error_code ec, std::size_t bytes){
                on_read(ec, bytes);
            });
        }

        // handle the incoming message and call do_read() again for next messages
        void on_read(beast::error_code ec, std::size_t){
            if(ec){
                if(ec != net::error::operation_aborted){
                    std::cerr << "Error in connection thread " << ec.message() << std::endl;
                }
                g_running = false;
                ioc_.stop();
                return;
            }

            auto arrival_time = std::chrono::system_clock::now().time_since_epoch();
            uint64_t local_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(arrival_time).count();
            std::string raw = beast::buffers_to_string(buffer_.data());
            handle_message(raw, local_time_ms);
            if(g_running) do_read();
        }

        // we try to parse the message as market data and if it is not market data(snapshot or delta), we try to parse it as a control message
        void handle_message(const std::string& raw, uint64_t local_time_ms){
            std::optional<ParsedUpdate> update;
            {
                telemetry::ScopedTimer parse_timer(telemetry::EventId::JsonParse);
                update = parse_message(raw_, *books_, local_time_ms, parser_);
            }
            if(update){
                handle_data_update(std::move(*update));
                return;
            }

            auto ctrl = parse_control_message(raw, parser_);
            if(ctrl){
                handle_control_message(*ctrl);
                return;
            }

            if(print_updates_ && raw.find("\type\":\"error\"") != std::string::npos){
                std::cerr << "[error] " << raw << "\n";
            }
        }

        // handling the market data
        void handle_data_update(ParsedUpdate update){
            if(!books_->is_active(update.book_index)){
                return;     // discarding for inactive books, we donot have to track them
            }

            auto it = sid_state_.find(update.sid);
            if(it != sid_state.end()){
                SidState& state = it->second;

                if(state.awaiting_baseline){
                    state.last_seq = update.seq;
                    state.awaiting_baseline = false;
                }
                else if(update.seq != state.last_seq + 1){
                    books_->at(update.book_index).mark_synced(false);
                    send_command(build_get_snapshot_json(nex_cmd_id_++, it->first, state.ticker));
                    state.awaiting_baseline = true;
                }
                else{
                    state.last_seq = update.seq;
                }
            }
            
            if(update.server_time_ms > 0 && update.local_time_ms >= update.server_time_ms){
                telemetry::record(telemetry::EventId::NetworkLatency, std::chrono::milliseconds(update.local_time_ms - update.server_time_ms));
            }

            while(!out_queue_.try_push(std::move(update))){
                std::this_thread::yield();
            }
        }

        // we pop the commands in the command_queue and add the ticker.
        void drain_commands(){
            std::vector<Command> cmds;
            commands_.drain(cmds);
            for(auto& cmd : cmds){
                if(cmd.type == CommandType::AddTicker) handle_add_ticker(cmd.ticker);
                else handle_remove_ticker(cmd.ticker);
            }
        }


        // for new ticker, we setup and send subscription message to kalshi.
        void handle_add_ticker(const std::string& ticker){
            if(ticker_to_sid_.count(ticker)) return;

            size_t index = books_->add_ticker(ticker);

            ParseUpdate activated;
            activated.type = ParsedUpdate::Type::TickerActivated;
            activated.book_index = index;
            activated.ticker_name = ticker;
            while(!out_queue_.try_push(std::move(activated))) std::this_thread::yield();

            int64_t cmd_id = next_cmd_id++;
            pending_subscribes_.emplace(cmd_id, PendingSubscribe{index, ticker});
            send_command(build_subscribe_json(cmd_id, ticker));
        }

        // remove ticker, we just deactivate in the books_, send the unsubscribe message.
        void handle_remove_ticker(const std::string& ticker){
            auto sid_it = ticker_to_sid_.find(ticker);
            if(sid_it == ticker_to_sid_.end()) return;

            int64_t sid = sid_it->second;
            size_t index = sid_state_.at(sid).book_index;

            books_->deactivate_ticker(ticker);

            ParsedUpdate deactivated;
            deactivated.type = ParsedUpdate::Type::TickerDeactivated;
            deactivated.book_index = index;
            deactivated.ticker_name = ticker;
            while(!out_queue_.try_push(std::move(deactivated))) std::this_thread::yield();

            send_command(build_unsubscribe_json(next_cmd_id_++, sid));

            sid_state_.erase(sid);
            ticker_to_sid_.erase(sid_it);
        }

        void handle_control_message(const ControlMessage& ctrl){
            switch(ctrl.type){
                case ControlMessage::Type::Subscribed: {
                    auto it = pending_subscribes_.find(ctrl.id);
                    if(it == pending_subscribes_.end() || ctrl.sid < 0) break;
                    sid_state_.emplace(ctrl.sid, SidState{it->second.book_index, it->second.ticker, 0, true});

                    ticker_to_sid_.emplace(it->second.ticker, ctrl.sid);
                    pending_subscribes_.erase(it);
                    break;
                }
                case ControlMessage::Type::Error:
                    std::cerr << "[Kalshi error] code = " << ctrl.error_code << "msg= " << ctrl.error_msg << "\n";
                    break;
                case ControlMessage::Type::Ok:
                case ControlMessage::Type::Unsubscribed:
                default:
                    break;
            }

            // we append to the pending_writes and try to write the message to Kalshi
            void send_command(std::string json_text){
                pending_writes_.push_back(std::move(json_text));
                try_write_next();
            }

            // here, we trying sending the message to Kalshi. since, Boost.Beast expects only one outstanding write
            // on a websocket stream at a time, we use the write_in_progress_ boolean
            // we define a lambda for the async write so when the write completes, the completion handler can set the boolean to false again.
            void try_write_next(){
                if(write_in_progress_ || pending_writes_.empty()) return;

                write_in_progress_ = true;
                auto msg = std::make_shared<str::string>(std::move(pending_writes_.front()));
                pending_writes_.pop_front();

                ws_.async_write(net::buffer(*msg), [this, msg](beast::error_code ec, std::size_t){
                    write_in_progress_ = false;
                    if(ec){
                        std::cerr << "Error sending command - " << ec.message() << std::endl;
                    }
                    try_write_next();
                })
            }

            static std::string build_subscribe_json(int64_t id, const std::string& ticker){
                json::object cmd;
                cmd["id"] = id;
                cmd["cmd"] = "subscribe";
                json::object params;
                params["channels"] = json::array{"orderbook_delta"};
                params["market_ticker"] = ticker;
                cmd["params"] = params;
                return json::serialize(cmd);
            }

            static std::string build_unsubscribe_json(int64_t ind, int64_t sid){
                json::object cmd;
                cmd["id"] = id;
                cmd["cmd"] = "unsubscribe";
                json::object params;
                params["sids"] = json::array{sid};
                cmd["params"] = params;
                return json::serialize(cmd);
            }

            static std::string build_get_snapshot_json(int64_t id, int64_t sid, const std::string& ticker){
                json::object cmd;
                cmd["id"] = id;
                cmd["cmd"] = "update_subscription";
                json::object params;
                params["sid"] = sid;
                params["market_tickers"] = json::array{ticker};
                params["action"] = "get_snapshot";
                cmd["params"] = params;
                return json::serialize(cmd);
            }

            struct SidState{
                size_t book_index = MultiOrderBook::kInvalidIndex;
                std::string ticker;
                uint64_t last_seq = 0;
                bool awaiting_baseline = true;
            }

            struct PendingSubscribe{
                size_t book_index;
                std::string ticker;
            }

            net::io_context& ioc_;
            websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
            std::shared_ptr<MultiOrderBook> books_;
            SPSCQueue<ParsedUpdate>& out_queue_;
            CommandQueue& commands_;
            bool print_updates_;

            simdjson::ondemand::parser parser_;
            beast::flat_buffer buffer_;

            std::unordered_map<int64_t, SidState> sid_state_;
            std::unordered_map<std::string, int64_t> ticker_to_sid_;
            std::unordered_map<int64_t, PendingSubscribe> pending_subscribes_;
            std::deque<std::string> pending_writes_;
            bool write_in_progress_ false;
            int64_t next_cmd_id_ = 1;
        }
};

void run_applier_thread(const std::shared_ptr<MultiOrderBook>& books, SPSCQueue<ParsedUpdate>& queue, bool print_updates){
    std::unordered_map<size_t, std::string> ticker_names;
    ParsedUpdate update;

    while(g_running || queue.size_approx() > 0){
        if(!queue.try_pop(update)){
            std::this_thread::yield();
            continue;
        }

        if(update.type == ParsedUpdate::Type::TickerActivated){
            ticker_names[update.book_index] = update.ticker_name;
            if(print_updates) std::cout << "[activated] " << update.ticker_name << " (index " << update.book_index << ")\n";
            continue; 
        }
        if(update.type == ParsedUpdate::Type::TickerDeactivated){
            if(print_updates) std::cout << "[deactivated] " << update.ticker_name << "\n";
            ticker_names.erase(update.book_index);
            continue;
        }
        if(update.type != ParsedUpdate::Type::Snapshot && update.type != ParsedUpdate::Type::Delta) continue;

        if(!books->is_active(update.book_index)) continue;

        if(update.type == ParsedUpdate::Type::Snapshot){
            telemetry::ScopedTimer apply_timer(telemetry::EventId::ApplySnapshot);
            apply_update(*books, update);
        }
        else{
            telemetry::ScopedTimer apply_timer(telemetry::EventId::ApplyDelta);
            apply_update(*books, update);
        }

        if(print_updates){
            auto name_it = ticker_names.find(update.book_index);
            const std::string& ticker = (name_it != ticker_names.end()) ? name_it->second : "<unknown>";

            if(update.type == ParsedUpdate::Type::Snapshot){
                std::cout << "[snapshot] " << ticker << " seq = " << update.seq << " yes_levels = " << update.yes_levels.size() << " no_levels =  " << update.no_levels.size() << "\n";
            }
            else{
                BookTop top = books->read_snapshot(update.book_index);
                std::cout << "[delta] " << ticker << "seq = " << update.seq << " yes_bid= " << top.yes_bid << " yes_ask= " << top.yes_ask << " no_bid = " << top.no_bid << "no_ask = " << top.no_ask << (top.is_synced ? "" : "(resyncing)") << "\n";
            }
        }
    }
}



// this is all the marker tickers that a connection handles
// later we will add core id to make the connection get handle by specific cpu core
struct FeedConfig{
    std::vector<std::string> market_tickers;
    bool print_updates = false;
};

// to be used by external users for sending add or remove commands.
class KalshiFeedHandle{
    public: 
        void attach(std::shared_ptr<CommandQueue> commands, std::shared_ptr<KalshiConnection> connection){
            commands_ = std::move(commands);
            connection_= std::move(connection);
        }

        void add_ticker(const std::string& ticker){
            commands_->push(Command{CommandType::AddTicker, ticker});
            connection_->notify_commands();
        }

        void remove_ticker(const std::string& ticker){
            commands_->push(Command{CommandType::RemoveTicker}, ticker);
            connection_->notify_commands();
        }
    private:
        std::shared_ptr<CommandQueue> commands_;
        std::shared_ptr<KalshiConnection> connection_;
};


void run_kalshi_feed(std::shared_ptr<MultiOrderBook> books, FeedConfig feed_config, std::shared_ptr<KalshiFeedHandle> out_handle = nullptr){
    if(feed_config.market_tickers.empty()){
        std::cerr << "Error: run_kalshi_feed called with no market_tickers" << std::endl;
        return ;
    }

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

        SPSCQueue<ParsedUpdate> data_queue(kQueueCapacity);
        auto commands = std::make_shared<CommandQueue>();

        auto connection = std::make_shared<KalshiConnection>(ioc, ssl_ctx, books, data_queue, *commands, feed_config.print_updates);
        connection->connect_and_handshake(config::api_key_id(), signature, timestamp);

        if(out_handle) out_handle->attach(commands, connection);

        net::signal_set signals(ioc, SIGINT);
        signals.async_wait([&](const beast::error_code&, int){
            g_running = false;
            connection->stop();
        });

        std::thread applied(run_applier_thread, books, std::ref(data_queue), feed_config.print_updates);

        connection->start(feed_config.market_tickers);
        ioc.run();

        g_running = false;
        applier.join();

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

    // to print the summary at exit
    telemetry::install_summary_atexit();

    // to catch Ctrl+C when stopping
    std::signal(SIGINT, signal_handler);

    std::vector<std::string> market_tickers(argv + 1, argv + argc);

    auto books = std::make_shared<MultiOrderBook>(market_tickers);
    run_kalshi_feed(books, FeedConfig{market_tickers, true});

    return 0;
}
#endif