#ifndef RAFT_HH
#define RAFT_HH

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <condition_variable>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <fstream>

#include "RaftRpcs.grpc.pb.h"
#include "raftCaller.hh"
#include "config.hh"
#include "utils.hh"
#include "applymsg.hh"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using RaftRpcsProto::RaftRpcs;
using RaftRpcsProto::AppendEntriesArgs;
using RaftRpcsProto::AppendEntriesReply;
using RaftRpcsProto::RequestVoteArgs;
using RaftRpcsProto::RequestVoteReply;
using RaftRpcsProto::InstallSnapshotArgs;
using RaftRpcsProto::InstallSnapshotReply;
using RaftRpcsProto::LogEntry;

/**
 * Raft 节点，实现算法相关
 * term，index从0开始
 */
class Raft final : public RaftRpcs::Service {

	using clock = std::chrono::high_resolution_clock;

	enum RaftRole{
		Follower,
		Candidate,
		Leader
	};

	// 持久化
	friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
        ar & currentTerm_;
        ar & votedFor_;
        ar & logs_;
        ar & lastSnapShotedLogIndex_;
        ar & lastSnapShotedLogTerm_;
    }
    
    // 持久化
    void persist();
    void readPersistedState();
    void saveSnapshot(const std::string& snapshot);
    void loadSnapshot();
    std::string snapshotFileName() const;
    std::string stateFileName() const;

public:
	Raft();
	~Raft();
	// 初始化Raft节点
	void init(
		std::vector<std::shared_ptr<RaftCaller>> peers,
		int thisNode,
		std::shared_ptr<LockedQueue<ApplyMsg>> applyCh
	);

	bool execute(Op command, int* newLogIndex, int* newLogTerm, bool* isLeader);

	// 发送心跳线程
	void heartBeatThread();
	// 超时选举
	void electionThread();
	// 将commit成功的log apply到状态机
	void applyThread();
	// 重置选举计时器
	void resetElectionTimer();
	// 将log apply到状态机
	void applyLog(ApplyMsg msg);

private:
	// 在Snapshot存在的情况下logIndex和logs_的索引不一定相等
	int getLogFromIndex(int logIndex);
	// 随机化选举间隔
	void randomizeElectionInterv();
	// 发起选举
	void doElection();
	// 获取最新Log的index和term
	std::pair<int, int> getLastLogState();
	// 发起requestVote
	void callRequestVoteThread(
		std::shared_ptr<RaftCaller> peer,
		int term,
		int candidateId,
		int lastLogIndex,
		int lastLogTerm,
		int &voteReceived
	);
	// 获取领导权
	void claimLeaderShip();
	// 获取AppendEntries需要的Prev参数
	std::pair<int, int> getPrevLogState(int i);
	// 对所有peer发起心跳，同时宣布Leadership
	void doHeartBeat();
	// 发起appenEntries
	void callAppendEntriesThread(
		int i,
		std::shared_ptr<RaftCaller> peer,
		int term,
		int leaderId,
		int prevLogIndex,
		int prevLogTerm,
		std::vector<std::shared_ptr<LogEntry>> logs,
		int leaderCommit,
		int &accepted
	);
	// 判断本地log是否存在和匹配给出的logindex和term
	bool searchLog(int index, int term);
	// 将快照发送给peer
	void callInstallSnapshotThread(
		std::shared_ptr<RaftCaller> peer
	);
	// 获取快照，删除冗余log
	int processSnapshot(std::string snapshot, int lastSnapshotIndex);

// 重载Service接口，执行服务端任务
public:
	Status appendEntries(ServerContext* context, const AppendEntriesArgs* args,
		AppendEntriesReply* reply) override;

	Status requestVote(ServerContext* context, const RequestVoteArgs* args,
		RequestVoteReply* reply) override;
	
	Status installSnapshot(ServerContext* context, const InstallSnapshotArgs* args,
		InstallSnapshotReply* reply) override;


// 实现状态
private:
	// 停止标志
	std::atomic_bool stop_;
    // 连接到其他raft节点的rpc caller
	std::vector<std::shared_ptr<RaftCaller>> peers_;
	// 最近一次重置选举计时器的时间点
    std::chrono::_V2::system_clock::time_point lastResetElectionTime_;
	// 最近一次重置心跳计时器的时间点
	std::chrono::_V2::system_clock::time_point lastHearBeatTime_;
	// 随机化的本机选举时间间隔
	std::chrono::duration<double, std::milli> electionInterv_;
	int totalNodeNum_, halfNodeNum_;
	
	// 线程同步相关
	std::mutex mtx_;
	// 用于callRequestVote线程同步
	std::condition_variable voteResult_;
	// 用于callAppendEntries线程同步
	std::condition_variable appendResult_;

	// 到KVServer的channel
	std::shared_ptr<LockedQueue<ApplyMsg>> applyCh_;

// 算法状态
private:
	// 本节点ID
	int thisNodeId_;
	// 本机当前term
	int currentTerm_;
    // 投票给的Candidate的Id
	int votedFor_;
	// 日志，使用shared_ptr就是认为leader的log不会在某一刻被认定失效
	std::vector<std::shared_ptr<LogEntry>> logs_;
	// 快照
	std::string snapshot_;
	// 当前角色
	RaftRole role_;
	// 快照包括的最后一条log信息，在log和快照都空时充当term和index的初始化
	int lastSnapShotedLogTerm_;
	int lastSnapShotedLogIndex_;
	// 日志复制
	std::vector<int> matchedIndex_;
	std::vector<int> nextIndex_;
	// 最后一个已提交的log
	int commitedIndex_;
	// 最后一个应用到状态机的log
	int lastApplied_;
};


#endif