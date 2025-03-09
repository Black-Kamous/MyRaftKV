#include "raft.hh"

Raft::Raft()
    : stop_(false)
{
}

Raft::~Raft(){
    stop_ = true;
}

void Raft::init(
    std::vector<std::shared_ptr<RaftCaller>> peers,
    int thisNode,
    std::shared_ptr<LockedQueue<ApplyMsg>> applyCh
)
{
    peers_ = peers;
    thisNodeId_ = peers[thisNode]->peerId;

    applyCh_ = applyCh;

    totalNodeNum_ = peers_.size();
    halfNodeNum_ = totalNodeNum_/2+1;

    currentTerm_ = 0;
    role_ = Follower;
    votedFor_ = -1;
    logs_.clear();
    lastSnapShotedLogIndex_ = -1;
    lastSnapShotedLogTerm_ = -1;
    nextIndex_.resize(totalNodeNum_, 0);
    matchedIndex_.resize(totalNodeNum_, 0);
    commitedIndex_ = -1;
    lastApplied_ = -1;
    resetElectionTimer();
    lastHearBeatTime_ = clock::now();

    //从持久化恢复

    std::thread election_T(&Raft::electionThread, this);
    election_T.detach();
    std::thread heartbeat_T(&Raft::heartBeatThread, this);
    heartbeat_T.detach();
    std::thread apply_T(&Raft::applyThread, this);
    apply_T.detach();
}

void Raft::applyThread(){
    while(stop_){
        std::this_thread::sleep_for(RaftConfig::applyInterv);
        std::vector<ApplyMsg> msgs;
        {
            std::unique_lock<std::mutex> lck(mtx_);
            while(lastApplied_ < commitedIndex_){
                lastApplied_++;
                auto logPtr = logs_[getLogFromIndex(lastApplied_)];
                ApplyMsg msg;
                msg.commandValid = true;
                msg.snapshotValid = false;
                msg.command = logPtr->command();
                msg.commandIndex = logPtr->logindex();
                msgs.push_back(msg);
            }
        }
        for(auto m:msgs){
            applyLog(m);
        }
    }
}

bool Raft::execute(Op command, int* newLogIndex, int* newLogTerm, bool* isLeader){
    std::unique_lock<std::mutex> lck(mtx_);
    if(role_ != Leader){
        *newLogIndex = -1;
        *newLogTerm = -1;
        *isLeader = false;
        return false;
    }

    auto [lastIndex, lastTerm] = getLastLogState();
    auto logPtr = std::make_shared<LogEntry>();
    logPtr->set_logterm(currentTerm_);
    logPtr->set_logindex(lastIndex + 1);
    logPtr->set_command(command.asString());
    logs_.push_back(logPtr);

    *newLogIndex = logPtr->logindex();
    *newLogTerm = logPtr->logterm();
    *isLeader = true;
    return true;
}

void Raft::applyLog(ApplyMsg msg){
    applyCh_->push(msg);
}

// 不加锁，注意在已经有锁的情况下调用该函数
int Raft::getLogFromIndex(int logIndex){
    auto [lastIndex, lastTerm] = getLastLogState();
    if(lastIndex < logIndex)
        return -1;
    return logIndex - lastSnapShotedLogIndex_ -1;
}

void Raft::heartBeatThread(){
    while(stop_){
        auto beforeSleepTime = clock::now();
        std::this_thread::sleep_for(RaftConfig::heartBeatInterv);
        if(role_ != Leader){
            std::this_thread::sleep_for(RaftConfig::heartBeatInterv * 4);
            continue;
        }else{
            std::unique_lock<std::mutex> lck(mtx_);
            if(lastHearBeatTime_ >= beforeSleepTime){
                continue;
            }
        }
        doHeartBeat();
    }
}

void Raft::electionThread()
{
    while(!stop_){
        auto beforeSleepTime = clock::now();
        std::this_thread::sleep_for(electionInterv_);
/**
 * 这里sleep(interv)，在醒来后检查时间点的方式和论文中异步定时器的描述
 * 并不严格相同，也许需要证明这种方式不会出现意外问题
 */
        // 不是Leader的节点才有机会发起选举
        if(role_ != Leader){
            std::unique_lock<std::mutex> lck(mtx_);
            if(lastResetElectionTime_ >= beforeSleepTime){
                continue;
            }
            // 超时了 goto doElection
        }else{
            continue;
        }
        doElection();
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
    auto waitStart = clock::now();
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
        auto elapsed = clock::now() - waitStart;
        if (elapsed >= RaftConfig::requestVoteTimeout) {
            // 选举超时
            break;
        }

        if (role_ == Candidate && currentTerm_ == myTerm && voteReceived >= halfNodeNum_) {
            claimLeaderShip();
            // 立即广播心跳巩固地位
            lck.unlock(); // 需要吗？
            doHeartBeat();
            break;
        }
    }
}

// 不加锁，注意在已经有锁的情况下调用该函数
std::pair<int, int> Raft::getLastLogState(){
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

// 不加锁，注意在已经有锁的情况下调用该函数
void Raft::claimLeaderShip(){
    role_ = Leader;
    auto [lastIndex, lastTerm] = getLastLogState();
    for(int i=0;i<nextIndex_.size();++i){
        nextIndex_[i] = lastIndex+1;
        matchedIndex_[i] = 0;
    }
}

// 不加锁，注意在已经有锁的情况下调用该函数
std::pair<int, int> Raft::getPrevLogState(int i){
    if(nextIndex_[i] == lastSnapShotedLogIndex_+1){
        return {lastSnapShotedLogIndex_, lastSnapShotedLogTerm_};
    }else{
        auto prev = getLogFromIndex(nextIndex_[i]);
        if(prev < 0) return {-1, -1};
        auto entry = logs_[prev];
        return {entry->logindex(), entry->logterm()};
    }
}

void Raft::doHeartBeat(){
    if(role_ != Leader){
        return;
    }
    int appendAccepted(1);
    for(int i=0;i<peers_.size();++i){
        std::unique_lock<std::mutex> lck(mtx_);
        auto peer = peers_[i];
        if(peer->peerId == thisNodeId_) continue;
        auto [prevIndex, prevTerm] = getPrevLogState(i);
        std::vector<std::shared_ptr<LogEntry>> argLogs;
        if(prevIndex > lastSnapShotedLogIndex_){
            for(int j=getLogFromIndex(prevIndex)+1; j<logs_.size(); ++j){
                argLogs.push_back(std::shared_ptr(logs_[j])); //注意这里应当是拷贝
            }
        }else{ 
            for(auto logPtr: logs_){
                argLogs.push_back(std::shared_ptr(logPtr));
            }
        }
        
        std::thread append_T(&Raft::callAppendEntriesThread,
            this,
            i,
            peer,
            currentTerm_,
            thisNodeId_,
            prevIndex,
            prevTerm,
            argLogs,
            commitedIndex_,
            std::ref(appendAccepted)
        );
        append_T.detach();
        // 一直到这里释放锁callAppendEntriesThread里面才能获取锁
    }
    lastHearBeatTime_ = clock::now();
}

void Raft::callAppendEntriesThread(
    int i,
    std::shared_ptr<RaftCaller> peer,
    int term,
    int leaderId,
    int prevLogIndex,
    int prevLogTerm,
    std::vector<std::shared_ptr<LogEntry>> logs,
    int leaderCommit,
    int &accepted
) {
    AppendEntriesReply reply;
    Status ret = peer->appenEntries(
        term,
        leaderId,
        prevLogIndex,
        prevLogTerm,
        logs,
        leaderCommit,
        &reply
    );
    if(ret.ok()){
        std::unique_lock<std::mutex> lck(mtx_);
        if(reply.term() > currentTerm_){ // 恢复为Follower
            role_ = Follower;
            currentTerm_ = reply.term();
            votedFor_ = -1;
            return;
        }
        if(role_ != Leader){    // 不是leader就没有要处理的
            return;
        }
        if(reply.success()){    // Follower接受了log
            accepted+=1;
            nextIndex_[i] = prevLogIndex + logs.size() + 1;
            matchedIndex_[i] = prevLogIndex + logs.size();
            if(accepted >= halfNodeNum_ && getLastLogState().second == currentTerm_){
                commitedIndex_ = std::max((unsigned long)commitedIndex_, prevLogIndex + logs.size());
            }
        }else{
            // 快速回退
            int confilictIndex = reply.missindex(), conflictTerm = reply.missterm();
            if(conflictTerm < 0){   // 代表Follower缺失日志
                nextIndex_[i] = confilictIndex;
            }else{
                int conflictOffset = -1;
                // 寻找Leader是否有 与Follower冲突位置term 相同的log
                for(int i=getLogFromIndex(prevLogIndex);i>=0;--i){
                    if(logs_[i]->logterm() == conflictTerm){
                        conflictOffset = i;
                        break;
                    }
                }
                if(conflictOffset < 0){     // 没找到
                    nextIndex_[i] = confilictIndex;
                }else{                      // 有相同term的log，将nextIndex置为紧挨着这个term的所有log的下一个index
                    nextIndex_[i] = logs_[conflictOffset+1]->logindex();
                }
            }

        }
    }
}

void Raft::resetElectionTimer()
{
    lastResetElectionTime_ = clock::now();
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

Status Raft::appenEntries(ServerContext* context, const AppendEntriesArgs* args,
    AppendEntriesReply* reply
){
    std::unique_lock<std::mutex> lck(mtx_);
    if(args->term() < currentTerm_){
        reply->set_term(currentTerm_);
        reply->set_success(false);
        return Status::OK;
    }

    if(args->term() > currentTerm_){
        // role_ = Follower;
        currentTerm_ = args->term();
        votedFor_ = -1;
    }

    role_ = Follower;
    resetElectionTimer();

    // 寻找是否有匹配
    auto [lastIndex, lastTerm] = getLastLogState();
    if(lastIndex < args->prevlogindex()){
        reply->set_term(currentTerm_);
        reply->set_success(false);
        reply->set_missindex(lastIndex+1);
        reply->set_missterm(-1);
        return Status::OK;
    }
    if(lastSnapShotedLogIndex_ > args->prevlogindex()){
        // 本机缺少snapshot
        reply->set_term(currentTerm_);
        reply->set_success(false);
        reply->set_missindex(lastSnapShotedLogIndex_+1);
        reply->set_missterm(-1);
        return Status::OK;
    }

    if(searchLog(args->prevlogindex(), args->prevlogterm())){
        // 可以append log
        for(int i=0;i<args->logs_size();++i){
            auto log = args->logs(i);
            if(log.logindex() > getLastLogState().first){
                logs_.push_back(std::make_shared<LogEntry>(log));
            }else{
                auto logPtr = logs_[getLogFromIndex(log.logindex())];
                if(logPtr->logterm() == log.logterm() && logPtr->command() != log.command()){
                    // 错误情况！应检查算法错误 忽略
                }
                if(logPtr->logterm() != log.logterm()){
                    logPtr->set_logterm(log.logterm());
                    logPtr->set_command(log.command());
                }
            }
        }

        if(args->leadercommit() > commitedIndex_){
            commitedIndex_ = std::min(getLastLogState().first, args->leadercommit());
        }

        reply->set_term(currentTerm_);
        reply->set_success(true);
    }else{
        // log存在但不匹配
        reply->set_term(currentTerm_);
        reply->set_success(false);
        int logOffset = getLogFromIndex(args->prevlogindex());
        int missTerm = logs_[logOffset]->logterm();
        int missIndex = args->prevlogindex();
        for(int i = logOffset;i>=0;++i){
            if(logs_[i]->logterm() == missTerm){
                missIndex = logs_[i]->logindex();
            }
        }
        reply->set_missterm(missTerm);
        reply->set_missindex(missIndex);
    }
    return Status::OK;
}

bool Raft::searchLog(int index, int term){
    if(index <= lastSnapShotedLogIndex_ || index > getLastLogState().first){
        return false;
    }
    return logs_[getLogFromIndex(index)]->logterm() == term;
}

Status Raft::requestVote(ServerContext* context, const RequestVoteArgs* args,
    RequestVoteReply* reply
){
    std::unique_lock<std::mutex> lck(mtx_);
    if(args->term() < currentTerm_){
        reply->set_term(currentTerm_);
        reply->set_votegranted(false);
        return Status::OK;
    }

    if(args->term() > currentTerm_){
        role_ = Follower;
        currentTerm_ = args->term();
        votedFor_ = -1;
    }

    if(votedFor_ == -1 || votedFor_ == args->candidateid()){
        // voteFor_是空或等于candidateId才有可能投票
        auto [lastIndex, lastTerm] = getLastLogState();
        if(lastTerm < args->lastlogterm() ||        // candidate的term更新或term相等，index更大时可以投票
            (lastTerm == args->lastlogterm() && lastIndex <= args->lastlogindex())){
                // 只在投出票时重置选举计时器
                resetElectionTimer();
                reply->set_term(currentTerm_);
                reply->set_votegranted(true);
                return Status::OK;
        }
    }
    reply->set_term(currentTerm_);
    reply->set_votegranted(false);
    return Status::OK;
    
}

Status Raft::installSnapshot(ServerContext* context, const InstallSnapshotArgs* args,
    InstallSnapshotReply* reply
){
    
}
