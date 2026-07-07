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

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

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
                throw std::runtime_erro("EVP_DigestSignUpdate failed: " + last_ssl_error());
            }
            
            size_t sig_len = 0;
            // calling with nullptr to just query the size
            if(EVP_DigestSignFinal(ctx, nullptr, &sig_len) <= 0){
                EVP_MD_free(ctx);
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
        static std::string last_ssl_erro(){
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



int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "missing command line arguments" << std::endl;
        return 1;
    }

    const std::string market_ticker = argv[1];

    try{
        // as per the Kalshi API Docs
        KalshiSigner signer(config::private_key_path());
       


    } catch(std::exception const& e){
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}