#include "raft.hh"

Raft::Raft()
    : stop_(false),
    votedFor_(-1),
    role_(RaftRole::Follower)
{
    // 随机化选举间隔
    randomizeElectionInterv();
}

Raft::~Raft(){
    stop_ = true;
}

void Raft::init()
{
}

void Raft::electionThread()
{
    while(!stop_){
        auto beforeSleepTime = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(electionInterv_);
/**
 * 这里sleep(interv)，在醒来后检查时间点的方式和论文中异步定时器的描述
 * 并不严格相同，也许需要证明这种方式不会出现意外问题
 */
        // 不是Leader的节点才有机会发起选举
        if(role_ != Leader){
            if(lastResetElectionTime_ >= beforeSleepTime){
                continue;
            }
            // 超时了
            doElection();
        }else{
            continue;
        }
    }
}

void Raft::doElection(){
    std::unique_lock<std::mutex> lck(mtx_);
    role_ = Candidate;
    // 开始新一轮选举
    currentTerm_ += 1;
    auto myTerm = currentTerm_;
    votedFor_ = thisNodeId_;
    resetElectionTimer();
    // 本节点变成Candidate之后不再接收新请求,因此lastLogIndex,lastLogTerm都只需要获取一次,用于多次requestVote中
    auto [lastIndex, lastTerm] = getLastLogState();
    // 在发起requestVote之前解锁
    lck.unlock();
    int voteReceived(1);
    for(auto peer: peers_){
        if(peer->peerId == thisNodeId_) continue;
        std::thread request_T (&Raft::callRequestVoteThread,
            this,
            peer,
            currentTerm_,
            thisNodeId_,
            lastIndex,
            lastTerm,
            std::ref(voteReceived)
        );
        request_T.detach();
    }
    // 判断voteReceived是否达到halfNodeNum_
    // 要求：线程安全，不发生死锁
    lck.lock();
    auto waitStart = std::chrono::steady_clock::now();
    while (true) {
        // 防止发生网络分区，只wait一段时间
        auto status = voteResult_.wait_for(
            lck, 
            RaftConfig::requestVoteTimeout,
            [&]() {
                return voteReceived >= halfNodeNum_ || 
                       role_ != Candidate
                       || currentTerm_ != myTerm;
            }
        );
    
        // 检查终止条件
        // 得票数未达预期/不再是Candidate/有其他线程更新了term（例如发起新的一轮election了）
        // 直接退出
        if (voteReceived < halfNodeNum_ || role_ != Candidate || currentTerm_ != myTerm) {
            break;
        }
    
        // 检查是否总等待时间超过requestVoteTimeout
        auto elapsed = std::chrono::steady_clock::now() - waitStart;
        if (elapsed >= RaftConfig::requestVoteTimeout) {
            // 选举超时
            break;
        }

        if (role_ == Candidate && currentTerm_ == myTerm && voteReceived >= halfNodeNum_) {
            role_ = Leader;
            // 立即广播心跳巩固地位
            doHeartBeat();
            break;
        }
    }
}

std::pair<int, int> Raft::getLastLogState(){
    // 不应加锁，又可能在已经有锁的情况下调用该函数
    if(logs_.size() > 0){
        auto lastLogPtr = logs_.back();
        return std::make_pair<int, int>(lastLogPtr->logindex(), lastLogPtr->logterm());
    }else{
        return {lastSnapShotedLogIndex_, lastSnapShotedLogTerm_};
    }
}

void Raft::callRequestVoteThread(
    std::shared_ptr<RaftCaller> peer,
    int term,
    int candidateId,
    int lastLogIndex,
    int lastLogTerm,
    int &voteReceived
){
    RequestVoteReply reply;
    Status ret = peer->requestVote(
        term,
        candidateId,
        lastLogIndex,
        lastLogTerm,
        &reply
    );
    if(ret.ok()){
        // 首先检查term
        std::unique_lock<std::mutex> lck(mtx_);
        if(reply.term() > currentTerm_){ // 恢复为Follower
            role_ = Follower;
            currentTerm_ = reply.term();
            votedFor_ = -1;
            return;
        }
        if(reply.votegranted())
            voteReceived+=1;
        voteResult_.notify_one();
    }
}

void Raft::doHeartBeat(){
    
}


void Raft::resetElectionTimer()
{
    lastResetElectionTime_ = std::chrono::high_resolution_clock::now();
    // 每次重置选举计时器都重新随机
    randomizeElectionInterv();
}

void Raft::randomizeElectionInterv() {
    // 随机化选举间隔
    auto u = std::uniform_int_distribution<milliseconds::rep>
        (RaftConfig::electionIntervLow.count(), RaftConfig::electionIntervHigh.count());
    auto gen = (std::mt19937(std::random_device{}()));
    electionInterv_ = std::chrono::milliseconds(u(gen));
}