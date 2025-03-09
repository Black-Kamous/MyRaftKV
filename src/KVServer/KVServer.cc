#include "KVServer.hh"

void KVServer::getApplyThread()
{
    while(stop_){
        // 没有参数的pop，一直阻塞
        auto appmsg = applyCh_->pop();
        if(appmsg->commandValid){
            executeCommand(appmsg);
        }
        if(appmsg->snapshotValid){
            executeSnapshot(appmsg);
        }
    }
}

KVServer::KVServer(
    int thisNodeId,
    std::string configFileName,
    short port
)
    : stop_(false),
    thisNodeId_(thisNodeId)
{
    applyCh_ = std::make_shared<LockedQueue<ApplyMsg>>();
    raftNode_ = std::make_shared<Raft>();

    // 监听rpc的线程
    std::thread t([this,port](){
            std::string server_address = absl::StrFormat("0.0.0.0:%d", port);
            grpc::EnableDefaultHealthCheckService(true);
            grpc::reflection::InitProtoReflectionServerBuilderPlugin();
            ServerBuilder builder;
            builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
            builder.RegisterService(this);
            builder.RegisterService(raftNode_.get());
            std::unique_ptr<Server> server(builder.BuildAndStart());
            std::cout << "Server listening on " << server_address << std::endl;
            server->Wait();
        }
    );
    t.detach();
    sleep(1);

    std::vector<std::shared_ptr<RaftCaller>> peers;

    NodeConfig nc(configFileName);
    for(int i=0;i<nc.confItems.size();++i){
        auto item = nc.confItems[i];
        std::string nodeName = item.first;
        std::string address = item.second;  // address的格式必须是 “ip:port”

        if(i == thisNodeId) {
            peers.push_back(nullptr);
            continue;
        }

        auto peerPtr = std::make_shared<RaftCaller>(
            grpc::CreateChannel(
                address,
                grpc::InsecureChannelCredentials()
            )
        );
        peers.push_back(peerPtr);
    }

    sleep(10);

    raftNode_->init(peers, thisNodeId, applyCh_);
    std::thread t1(
        &KVServer::getApplyThread
    );
    t1.join();
}

void KVServer::executeCommand(std::shared_ptr<ApplyMsg> msg)
{
    Op op;
    op.fromString(msg->command);

    if(!isDuplicate(op.clientId, op.requestId)){
        if(op.operation == "Put"){
            executePutOnKV(op);
        }else if(op.operation == "Append"){
            executeAppendOnKV(op);
        }
    }

    informExecuted(op, msg->commandIndex);
}

void KVServer::executeSnapshot(std::shared_ptr<ApplyMsg> msg)
{
}

bool KVServer::isDuplicate(std::string clientId, int requestId)
{
    if(lastRequestId_.find(clientId) == lastRequestId_.end()){
        return false;
    }
    return requestId <= lastRequestId_[clientId];
}

bool KVServer::informExecuted(Op op, int index)
{
    std::unique_lock<std::mutex> lck(mtx_);
    if(waitApplyCh_.find(index) == waitApplyCh_.end()){
        return false;
    }
    auto lq = waitApplyCh_[index];
    lck.unlock();
    lq->push(op);
    return true;
}

void KVServer::executePutOnKV(Op op)
{
    sm.put(op.key, op.value);
    std::unique_lock<std::mutex> lck(mtx_);
    lastRequestId_[op.clientId] = std::max(lastRequestId_[op.clientId], op.requestId);
}

void KVServer::executeAppendOnKV(Op op)
{
    sm.append(op.key, op.value);
    std::unique_lock<std::mutex> lck(mtx_);
    lastRequestId_[op.clientId] = std::max(lastRequestId_[op.clientId], op.requestId);
}

Status KVServer::get(ServerContext *context, const GetArgs *args, GetReply *reply)
{
    Op op;
    op.operation = "Get";
    op.key = args->key();
    op.clientId = args->clientid();
    op.requestId = args->requestid();

    int newIndex=-1, newTerm=-1;
    bool isLeader = false;

    raftNode_->execute(op, &newIndex, &newTerm, &isLeader);

    if(!isLeader){
        reply->set_retcode(ReturnCode::NOT_LEADER);
        return Status::OK;
    }

    std::unique_lock<std::mutex> lck(mtx_);
    if(waitApplyCh_.find(newIndex) == waitApplyCh_.end()){
        waitApplyCh_[newIndex] = std::make_shared<LockedQueue<Op>>();
    }

    auto waitQueue = waitApplyCh_[newIndex];
    lck.unlock();

    auto [ok, res] = waitQueue->top(ServerConfig::consensusTimeout);
    if(!ok){
        if(isDuplicate(op.clientId, op.requestId)){ // 重复的get也可以执行
            auto [ok, res] = executeGetOnKV(op);
            if(ok){
                reply->set_retcode(ReturnCode::DONE);
                reply->set_value(res);
            }else{
                reply->set_retcode(ReturnCode::NO_KEY);
                reply->set_value("");
            }
        }else{
            reply->set_retcode(ReturnCode::NOT_LEADER);
        }
    }else{
        if(res->clientId == op.clientId && res->requestId == op.requestId){ // 再次验证防止Leader换人
            auto [ok, res] = executeGetOnKV(op);
            if(ok){
                reply->set_retcode(ReturnCode::DONE);
                reply->set_value(res);
            }else{
                reply->set_retcode(ReturnCode::NO_KEY);
                reply->set_value("");
            }
        }else{
            reply->set_retcode(ReturnCode::NOT_LEADER);
        }
    }

    lck.lock();
    waitApplyCh_.erase(newIndex);
    return Status::OK;
}

Status KVServer::putAppend(ServerContext *context, const PutAppendArgs *args, PutAppendReply *reply)
{
    Op op;
    op.operation = args->op();
    op.key = args->key();
    op.value = args->value();
    op.clientId = args->clientid();
    op.requestId = args->requestid();

    int newIndex=-1, newTerm=-1;
    bool isLeader = false;

    raftNode_->execute(op, &newIndex, &newTerm, &isLeader);

    if(!isLeader){
        reply->set_retcode(ReturnCode::NOT_LEADER);
        return Status::OK;
    }

    std::unique_lock<std::mutex> lck(mtx_);
    if(waitApplyCh_.find(newIndex) == waitApplyCh_.end()){
        waitApplyCh_[newIndex] = std::make_shared<LockedQueue<Op>>();
    }

    auto waitQueue = waitApplyCh_[newIndex];
    lck.unlock();

    auto [ok, res] = waitQueue->top(ServerConfig::consensusTimeout);
    if(!ok){
        if(isDuplicate(op.clientId, op.requestId)){ // 重复的put/append视为成功
            reply->set_retcode(ReturnCode::DONE);
        }else{
            reply->set_retcode(ReturnCode::NOT_LEADER);
        }
    }else{
        if(res->clientId == op.clientId && res->requestId == op.requestId){ // 再次验证防止Leader换人
            reply->set_retcode(ReturnCode::DONE);
        }else{
            reply->set_retcode(ReturnCode::NOT_LEADER);
        }
    }

    lck.lock();
    waitApplyCh_.erase(newIndex);
    return Status::OK;
}

std::pair<bool, std::string> KVServer::executeGetOnKV(Op op)
{
    auto res = sm.get(op.key);
    std::unique_lock<std::mutex> lck(mtx_);
    lastRequestId_[op.clientId] = std::max(lastRequestId_[op.clientId], op.requestId);
    if(res == ""){
        return {false, ""};
    }else{
        return {true, res};
    }
}
