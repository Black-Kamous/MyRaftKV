#ifndef UTILS_HH
#define UTILS_HH

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <functional>
#include <iostream>

template <typename T>
class LockedQueue {
    using milli_duration = std::chrono::duration<double, std::milli>;
public:
    LockedQueue(){};

    void push(T&& arg){
        std::unique_lock<std::mutex> lck(mtx_);
        queue_.push(std::forward<T>(arg));
        notEmpty_.notify_one();
    }

    void pop(){
        std::unique_lock<std::mutex> lck(mtx_);
        notEmpty_.wait(lck);
        queue_.pop();
    }

    bool pop(milli_duration tmout){
        std::unique_lock<std::mutex> lck(mtx_);
        if(std::cv_status::timeout == notEmpty_.wait_for(lck, tmout)){
            return false;
        }else{
            queue_.pop();
            return true;
        }
    }

    std::shared_ptr<T> top() {
        std::unique_lock<std::mutex> lck(mtx_);
        notEmpty_.wait(lck);
        return std::make_shared<T>(queue_.front());
    }

    std::pair<bool, std::shared_ptr<T>> top(milli_duration tmout){
        std::unique_lock<std::mutex> lck(mtx_);
        if(std::cv_status::timeout == notEmpty_.wait_for(lck, tmout)){
            return {false, nullptr};
        }else{
            return {true, std::make_shared<T>(queue_.front())};
        }
    }

    std::queue<T> queue_;
    std::mutex mtx_;
    std::condition_variable notEmpty_;

};

#endif
