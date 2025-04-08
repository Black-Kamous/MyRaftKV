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
    // 序列化相关
private:
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
        ar & stm_; // 直接序列化unordered_map
    }

public:
    std::string serialize() {
        std::ostringstream oss;
        {
            std::shared_lock<std::shared_mutex> lck(mtx_);
            boost::archive::binary_oarchive oa(oss);
            oa << *this; // 调用成员serialize方法
        }
        return oss.str();
    }

    void applySnapshot(const std::string& snapshot) {
        std::istringstream iss(snapshot);
        boost::archive::binary_iarchive ia(iss);
        std::unique_lock<std::shared_mutex> lck(mtx_);
        ia >> *this; // 调用成员serialize方法
    }


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
    // 将raftnode传来的快照作用到状态机
    void executeSnapshot(std::shared_ptr<ApplyMsg> msg);
    // 判断请求是否重复
    bool isDuplicate(std::string clientId, int requestId);
    // 将数据库操作执行到KV数据库
    std::pair<bool, std::string> executeGetOnKV(Op op);
    void executePutOnKV(Op op);
    void executeAppendOnKV(Op op);
    // 将操作完成的消息发送给waitApplyCh
    bool informExecuted(Op op, int index);
    // 监听RPC
    void listenRPCThread(int port);
    // 制作快照
    std::string makeSnapshot();
    // 判断是否应该制作快照
    bool isTimeToSnapshot();

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