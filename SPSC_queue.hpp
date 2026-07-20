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

        bool try_push(T&& item){
            const size_t head = head_.load(std::memory_order_relaxed);
            const size_t next = advance(head);

            if(next == tail_.load(std::memory_order_acquire)){
                return false;
            }

            buffer_[head] = std::move(item);
            head_.store(next, std::memory_order_release);
            return true;
        }

        SPSCQueue(const SPSCQueue&) = delete;
        SPSCQueue& operator=(const SPSCQueue&) = delete;
    
    private:
        const size_t capacity_;
        std::vector<T> buffer;
        std::atomic<size_t> head_{0};
        std::atomic<size_t> tail_{0};

        size_t advance(size_t idx) const {return (idx + 1) % capacity_;}
};