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
        void push(Command cmd){
            std::lock_guard<std::mutex> lock(mu_);
            queue_.push_back(std::move(cmd));
        }

        void drain(std::vector<Command>& out){
            std::lock_guard<std::mutex> lock(mu_);
            out.insert(out.end(), std::make_move_iterator(queue_.begin()), std::make_move_iterator(queue_.end()));
            queue_.clear();
        }
    private:
        std::mutex mu_;
        std::deque<Command> queue_;
};