#pragma once

#include <atomic>
#include <cstddef>
#include <vector>
#include <utility>

template <typename T>
class SPSCQueue{
    public:
        explicit SPSCQueue(size_t capacity) : capacity_(capacity + 1), buffer_(capacity){

        }

        SPSCQueue(const SPSCQueue&) = delete;
        SPSCQueue& operator=(const SPSCQueue&) = delete;
    
    private:
        const size_t capacity_;
        std::vector<T> buffer;
        std::atomic<size_t> head_{0};
        std::atomic<size_t> tail_{0};
};