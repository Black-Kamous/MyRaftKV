#ifndef UTILS_HH
#define UTILS_HH

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>


template <typename T>
class LockedQueue {
    using milli_duration = std::chrono::duration<double, std::milli>;
public:
    LockedQueue(){};

    void push(const T& arg){
        std::unique_lock<std::mutex> lck(mtx_);
        queue_.push(arg);
        notEmpty_.notify_one();
    }

    std::shared_ptr<T> pop(){
        std::unique_lock<std::mutex> lck(mtx_);
        notEmpty_.wait(lck);
        auto data = std::make_shared<T>(queue_.front());
        queue_.pop();
        return data;
    }

    std::pair<bool, std::shared_ptr<T>> pop(milli_duration tmout){
        std::unique_lock<std::mutex> lck(mtx_);
        if(std::cv_status::timeout == notEmpty_.wait_for(lck, tmout)){
            return {false, nullptr};
        }else{
            auto data = std::make_shared<T>(queue_.front());
            queue_.pop();
            return {true, data};
        }
    }

    std::shared_ptr<T> top() {
        std::unique_lock<std::mutex> lck(mtx_);
        notEmpty_.wait(lck, 
            [this](){
                return !queue_.empty();
            }
        );
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

class Op {
public:
    // Your definitions here.
    // Field names must start with capital letters,
    // otherwise RPC will break.
    std::string operation;  // "Get" "Put" "Append"
    std::string key;
    std::string value;
    std::string clientId;  //客户端号码
    int requestId;         //客户端号码请求的Request的序列号，为了保证线性一致性
                        // IfDuplicate bool // Duplicate command can't be applied twice , but only for PUT and APPEND

public:
    // todo
    //为了协调raftRPC中的command只设置成了string,这个的限制就是正常字符中不能包含|
    //当然后期可以换成更高级的序列化方法，比如protobuf
    std::string asString() const {
        return "Operation{" + operation + "},Key{" + key + "},Value{" + value + "},ClientId{" +
                clientId + "},RequestId{" + std::to_string(requestId) + "}";
    }
    
    // 反序列化静态方法
    void fromString(const std::string& str) {
        std::vector<std::string> parts;
        std::istringstream iss(str);
        std::string token;

        // 第一步：按逗号分割字符串
        while (std::getline(iss, token, ',')) {
            // 移除首尾空格（如果有）
            token.erase(token.begin(), std::find_if(token.begin(), token.end(), [](int ch) {
                return !std::isspace(ch);
            }));
            token.erase(std::find_if(token.rbegin(), token.rend(), [](int ch) {
                return !std::isspace(ch);
            }).base(), token.end());
            
            parts.push_back(token);
        }

        // 第二步：解析每个键值对
        for (const auto& part : parts) {
            size_t openBrace = part.find('{');
            size_t closeBrace = part.find('}');
            
            if (openBrace != std::string::npos && 
                closeBrace != std::string::npos &&
                openBrace < closeBrace) {
                
                std::string field = part.substr(0, openBrace);
                std::string value = part.substr(openBrace+1, closeBrace-openBrace-1);

                // 第三步：根据字段名赋值
                if (field == "Operation") {
                    operation = value;
                } else if (field == "Key") {
                    key = value;
                } else if (field == "Value") {
                    value = value;
                } else if (field == "ClientId") {
                    clientId = value;
                } else if (field == "RequestId") {
                    try {
                        requestId = std::stoi(value);
                    } catch (...) {
                        // 处理无效数字
                        throw std::runtime_error("Invalid RequestId format");
                    }
                }
            }
        }
    }

public:
    friend std::ostream& operator<<(std::ostream& os, const Op& obj) {
        os << "[MyClass:Operation{" + obj.operation + "},Key{" + obj.key + "},Value{" + obj.value + "},ClientId{" +
                    obj.clientId + "},RequestId{" + std::to_string(obj.requestId) + "}";  // 在这里实现自定义的输出格式
        return os;
    }
};
   

#endif
