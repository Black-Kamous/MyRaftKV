#ifndef KVSERVER_HH
#define KVSERVER_HH

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <condition_variable>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "KVServerRpcs.grpc.pb.h"
#include "raft/raft.hh"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using KVServerRpcsProto::KVServerRpcs;
using KVServerRpcsProto::ReturnCode;
using KVServerRpcsProto::GetArgs;
using KVServerRpcsProto::GetReply;
using KVServerRpcsProto::PutAppendArgs;
using KVServerRpcsProto::PutAppendReply;

class StateMachine {
public:
    StateMachine(){}
    ~StateMachine(){};

    void put(std::string key, std::string value){
        std::unique_lock<std::shared_mutex> lck(mtx_);
        stm_[key] = value;
    }

    void append(std::string key, std::string value){
        std::unique_lock<std::shared_mutex> lck(mtx_);
        stm_[key] = value;
    }

    std::string get(std::string key){
        std::shared_lock<std::shared_mutex> lck(mtx_);
        if(stm_.find(key) == stm_.end()){
            return "";
        }
        return stm_[key];
    }

private:
    std::shared_mutex mtx_;
    std::unordered_map<std::string, std::string> stm_;
};

class KVServer final : public KVServerRpcs::Service {
public:
    KVServer(
        int thisNodeId_,
        std::string configFileName,
        short port
    );
    ~KVServer(){};


private:
    // 一直从raft节点获取applymsg
    void getApplyThread();
    // 执行applymsg
    void executeCommand(std::shared_ptr<ApplyMsg> msg);
    void executeSnapshot(std::shared_ptr<ApplyMsg> msg);
    // 
    bool isDuplicate(std::string clientId, int requestId);
    // 
    std::pair<bool, std::string> executeGetOnKV(Op op);
    void executePutOnKV(Op op);
    void executeAppendOnKV(Op op);
    //
    bool informExecuted(Op op, int index);

private:
    bool stop_;
    std::mutex mtx_;
    StateMachine sm;
    // 和raft节点共享的applyCh
    std::shared_ptr<LockedQueue<ApplyMsg>> applyCh_;
    // 保存各client的最新requestid
    std::unordered_map<std::string, int> lastRequestId_; 
    // 在这里等待server的操作结果
    std::unordered_map<int, std::shared_ptr<LockedQueue<Op>>> waitApplyCh_;
    std::shared_ptr<Raft> raftNode_;
    int thisNodeId_;

// 重载Service接口，执行服务端任务
public:
    Status get(ServerContext* context, const GetArgs* args,
        GetReply* reply) override;
    Status putAppend(ServerContext* context, const PutAppendArgs* args,
        PutAppendReply* reply) override;

};





#endif