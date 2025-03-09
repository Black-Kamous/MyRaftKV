#include "KVClerk.hh"

Status KVCaller::get(std::string key, std::string clientId, int requestId, GetReply *reply)
{
    GetArgs args;
    args.set_key(key);
    args.set_clientid(clientId);
    args.set_requestid(requestId);

    ClientContext context;
    stub_->get(&context, args, reply);
}

Status KVCaller::putAppend(std::string key, std::string value, std::string op, std::string clientId, int requestId, PutAppendReply *reply)
{
    PutAppendArgs args;
    args.set_key(key);
    args.set_value(value);
    args.set_op(op);
    args.set_clientid(clientId);
    args.set_requestid(requestId);

    ClientContext context;
    stub_->putAppend(&context, args, reply);
}

KVClerk::KVClerk(std::string configFileName)
    :clientId_(randomId()),
    requestId_(0),
    recentLeaderId_(0)
{
    NodeConfig nc(configFileName);
    for(int i=0;i<nc.confItems.size();++i){
        auto item = nc.confItems[i];
        std::string nodeName = item.first;
        std::string address = item.second;  // address的格式必须是 “ip:port”

        auto peerPtr = std::make_shared<KVCaller>(
            grpc::CreateChannel(
                address,
                grpc::InsecureChannelCredentials()
            )
        );
        servers_.push_back(peerPtr);
    }
}

std::string KVClerk::randomId()
{
    return std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand());
}

std::string KVClerk::get(std::string key)
{
    auto requestId = ++requestId_;
    int sid = recentLeaderId_;

    GetReply reply;
    while(true){
        auto server = servers_[sid];
        auto ret = server->get(key, clientId_, requestId, &reply);
        if(ret.ok()){
            if(reply.retcode() == ReturnCode::DONE){
                recentLeaderId_ = sid;
                return reply.value();
            }else if(reply.retcode() == ReturnCode::NO_KEY){
                return "";
            }
        }
        sid = (sid+1)%servers_.size();
    }
    return "Not Reachable";
}

bool KVClerk::put(std::string key, std::string value)
{
    return putAppend(key, value, "Put");
}

bool KVClerk::append(std::string key, std::string value)
{
    return putAppend(key, value, "Append");
}

bool KVClerk::putAppend(std::string key, std::string value, std::string op)
{
    auto requestId = ++requestId_;
    int sid = recentLeaderId_;

    PutAppendReply reply;
    while(true){
        auto server = servers_[sid];
        auto ret = server->putAppend(key, value, op, clientId_, requestId, &reply);
        if(ret.ok()){
            if(reply.retcode() == ReturnCode::DONE){
                recentLeaderId_ = sid;
                return true;
            }
        }
        sid = (sid+1)%servers_.size();
    }
    return false;
}