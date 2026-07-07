#include <openssl/ssl.h>
#include <openssl/err.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>

static constexpr const char *HOST = "external-api-ws.demo.kalshi.co";
static constexpr const char *PORT = "443";
static constexpr const char *PATH = "/trade-api/ws/v2";

static constexpr size_t HTTP_REQ_CAP = 4096;
static constexpr size_t HTTP_RX_CAP = 16384;
static constexpr size_t WS_RX_CAP = 1 << 20;
static constexpr size_t WS_TX_CAP = 1 << 16;
static constexpr size_t MAX_WS_PAYLOAD = 1 << 20;

static uint64_t now_ms(){
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static bool set_nonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool wait_connect_done(int fd, int timeout_ms){
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;

    int rc = poll(&pfd, 1, timeout_ms);
    if(rc <= 0) return false;

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0) return false;
    if(so_error != 0){
        errno = so_error;
        return false;
    }
    return true;
}

static int connect_tcp_nonblocking(const char *host, const char *port){
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *res = nullptr;
    int rc = getaddrinfo(host, port, &hints, &res);
    if(rc != 0){
        std::cerr << "getaddrinfo failed: " << gai_strerror(rc) << "\n";
        return -1;
    }

    int fd = -1;
    for(addrinfo *it = res; it; it = it->ai_next){
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(fd < 0) continue;

        if(!set_nonblocking(fd)){
            close(fd);
            fd = -1;
            continue;
        }

        if(connect(fd, it->ai_addr, it->ai_addrlen) == 0){
            freeaddrinfo(res);
            return fd;
        }

        if(errno == EINPROGRESS){
            freeaddrinfo(res);
            return fd;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfor(res);
    return -1;
}

int main(int argc, char **argv){
    const char *api_key = (argc > 1) ? argv[1] : "API_KEY";
    const char *private_key_path = (argc > 2) ? argv[2] : "PRIVATE_KEY_PEM_PATH";
    const char *market_ticker = (argc > 3) ? argv[3] : "TICKER";

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    int fd = connect_tcp_nonblocking(HOST, PORT);

    if(fd < 0){
        std::cerr << "connect setup failed" << std::endl;
        return 1;
    }


    if(!wait_connect_done(fd, 10000)){
        std::cerr << "tcp connect timeout/failure\n";
        close(fd);
        return 1;
    }

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if(!ctx){
        std::cerr << "SSL_CTX_new failed\n";
        close(fd);
        return 1;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    SSL *ssl = SSL_new(ctx);
    if(!ssl){
        std::cerr << "SSL_new failed\n";
        SSL_CTX_free(ctx);
        close(fd);
        return 1;
    }

    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, HOST);

    
}