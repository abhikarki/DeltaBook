#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

enum class CommandType{
    AddTicker,
    RemoveTicker
};

struct Command{
    CommandType type;
    std::string ticker;
};

class CommandQueue{
    public:
    private:
        std::mutex mu_;
        std::deque<Command> queue_;
};