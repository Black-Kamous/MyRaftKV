#ifndef KVCLERK_HH
#define KVCLERK_HH

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

class KVCaller final {
// 发起Rpc call
public:
    Status get(
        std::string key,
        std::string clientId,
        int requestId,
        GetReply* reply
    );

    Status putAppend(
        std::string key,
        std::string value,
        std::string op,
        std::string clientId,
        int requestId,
        PutAppendReply* reply
    );


private:
    std::unique_ptr<KVServerRpcs::Stub> stub_;
};

class KVClerk final {
public:
    KVClerk(std::string configFileName);
    ~KVClerk(){};

    std::string get(std::string key);

    bool put(std::string key, std::string value);

    bool append(std::string key, std::string value);

private:
    std::string randomId();

    bool putAppend(std::string key, std::string value, std::string op);

private:
    std::vector<std::shared_ptr<KVCaller>> servers_;
    std::string clientId_;
    int requestId_;
    int recentLeaderId_;
};






#endif