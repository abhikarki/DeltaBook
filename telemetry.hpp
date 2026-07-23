#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>

namespace telemetry{
    enum class EventId : size_t{
        JsonParse = 0,
        IndexResolve,
        ApplySnapshot,
        ApplyDelta,
        WsRead,
        NetworkLatency,
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
            case EventId::NetworkLatency: return "Network Latency";
            case EventId::WsRead: return "Websocket Socket Read";
            case EventId::JsonParse: return "simdjson parse";
            case EventId::IndexResolve: return "market_ticker -> index_for";
            case EventId::ApplySnapshot: return "Apply Baseline Snapshot to Book";
            case EventId::ApplyDelta: return "Apply Delta to Book";
            default: return "Unknown";
        }
    }

    inline void print_summary(){
        std::cout << std::left << std::setw(30) << "event" << std::right << std::setw(12) << "count" << std::setw(16) << "avg(us)" << std::setw(16) << "min(us)" << std::setw(16) << "max (us)" << "\n";

        for(size_t i = 0; i < static_cast<size_t>(EventId::Count); i++){
            const auto& stats = g_stats[i];
            if(stats.count == 0) continue;

            double avg = (static_cast<double>(stats.total_ns) / stats.count) / 1000.0;
            double min_us = static_cast<double>(stats.min_ns) / 1000.0;
            double max_us = static_cast<double>(stats.max_ns) / 1000.0;
            std::cout << std::left << std::setw(30) << event_name(static_cast<EventId>(i)) << std::right << std::setw(12) << stats.count << std::setw(16) << std::setprecision(3) << avg << std::setw(16) << min_us << std::setw(16) << max_us << "\n";
        }
    }

    inline void install_summary_atexit(){
        static bool installed = false;
        if(!installed){
            std::atexit(print_summary);
            installed = true;
        }
    }
}