#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

constexpr int64_t PRICE_TICKS_PER_DOLLAR = 10000;

// converting the floating point data to whole number for different types of data (price & volume)
inline int64_t parse_fixed(const std::string& s, int decimals){
    bool neg = false;
    size_t i = 0;
    if(!s.empty() && (s[0] == '-' || s[0] == '+')){
        neg = (s[0] == '-');
        i = 1;
    }

    auto dot = s.find('.', i);
    std::string int_part = (dot == std::string::npos) ? s.substr(i) : s.substr(i, dot - i);
    std::string frac_part = (dot == std::string::npos) ? "" : s.substr(dot + 1);

    if(static_cast<int>(frac_part.size()) < decimals){
        frac_part.append(decimals - frac_part.size(), '0');
    }
    else {
        frac_part = frac_part.substr(0, decimals);
    }

    int64_t whole = int_part.empty() ? 0 : std::stoll(int_part);
    int64_t frac = frac_part.empty() ? 0 : std::stoll(frac_part);
    int64_t scale = 1;
    for(int k = 0; k < decimals; k++) scale *= 10;

    int64_t value = whole * scale + frac;
    return neg ? -value : value;
}

inline int64_t parse_price(const std::string& s) {return parse_fixed(s, 4);}
inline int64_t parse_size(const std::string& s) {return parse_fixed(s, 2);}

enum class Side{
    Yes,
    No
};

// single update received from Kashi
struct OrderBookDelta{
    Side side;
    int64_t price_ticks;
    int64_t size_delta;
    uint64_t seq = 0;
};

// since price level in order book
struct PriceLevel{
    int64_t price_ticks;
    int64_t size;
};

// top levels of book
struct BookTop{
    int64_t yes_bid = -1, yes_ask = -1;
    int64_t no_bid = -1, no_ask = -1;
    bool is_synced = false;
};

class OrderBook{
    public:
        bool is_synced() const {return synced_;}
        uint64_t last_seq() const {return last_seq_;}
        uint64_t _gap_count() const {return gap_count_;}

        void apply_snapshot(const std::vector<PriceLevel>& yes_levels, const std::vector<PriceLevel>& no_levels, uint64_t seq){
            yes_.clear();
            no_.clear();
            for(auto const& lvl : yes_levels) if(lvl.size > 0) yes_[lvl.price_ticks] = lvl.size;
            for(auto const& lvl : no_levels) if(lvl.size > 0) no_[lvl.price_ticks] = lvl.size;
            last_seq_ = seq;
            synced_ = true;
        }

        void apply_delta(const OrderBookDelta& d){
            check_seq(d.seq);

            auto& book = (d.side == Side::Yes) ? yes_ : no_;
            auto it = book.find(d.price_ticks);
            int64_t current = (it == book.end()) ? 0 : it->second;
            int64_t updated = current + d.size_delta;

            if(updated <= 0){
                if(it != book.end()) book.erase(it);
            }else{
                book[d.price_ticks] = updated;
            }
        }

        // create and return the implied level for the book top
        BookTop get_snapshot() const {
            BookTop top;
            if(!yes_.empty()) top.yes_bid = yes_.rbegin()->first;
            if(!no_.empty()) top.no_bid = no_.rbegin()->first;

            if(top.no_bid >= 0) top.yes_ask = PRICE_TICKS_PER_DOLLAR - top.no_bid;
            if(top.yes_bid >= 0) top.no_ask = PRICE_TICKS_PER_DOLLAR - top.yes_bid;
            top.is_synced = synced_;
            return top;
        }   

    private:
        // just regular yes no book for now and not the implied book
        std::map<int64_t, int64_t> yes_;
        std::map<int64_t, int64_t> no_;
        uint64_t last_seq_ = 0;
        bool synced_ = true;
        uint64_t gap_count_ = 0;

        // check sequence number for gaps, for now just updating synced_ to false and not refreshing the snapshot
        void check_seq(uint64_t seq){
            if(last_seq_ != 0 && seq != last_seq_ + 1){
                synced_ = false;
                gap_count_++;
            }
            last_seq_ = seq;
        }
};


class SharedOrderBook{
    public:
        void update(const OrderBookDelta& d){
            std::lock_guard<std::mutex> lock(mu_);
            book_.apply_delta(d);
        }

        void load_snapshot(const std::vector<PriceLevel>& yes_levels, const std::vector<PriceLevel>& no_levels, uint64_t seq){
            std::lock_guard<std::mutex> lock(mu_);
            book_.apply_snapshot(yes_levels, no_levels, seq);
        }

        BookTop read_snapshot() const{
            std::lock_guard<std::mutex> lock(mu_);
            return book_.get_snapshot();
        }

    private:
        mutable std::mutex mu_;
        OrderBook book_;
};
