#ifndef RAFTCALLER_HH
#define RAFTCALLER_HH

#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpcpp/grpcpp.h>
#include "RaftRpcs.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using RaftRpcsProto::RaftRpcs;
using RaftRpcsProto::AppendEntriesArgs;
using RaftRpcsProto::AppendEntriesReply;
using RaftRpcsProto::RequestVoteArgs;
using RaftRpcsProto::RequestVoteReply;
using RaftRpcsProto::InstallSnapshotArgs;
using RaftRpcsProto::InstallSnapshotReply;
using RaftRpcsProto::LogEntry;

class RaftCaller final {
public:
    RaftCaller(std::shared_ptr<Channel> channel)
    : stub_(RaftRpcs::NewStub(channel)) {}



// 发起Rpc call
public:
Status appenEntries(int term,
    int leaderId,
    int prevLogIndex,
    int prevLogTerm,
    std::vector<std::shared_ptr<LogEntry>> logs,
    int leaderCommit,
    AppendEntriesReply* reply);

Status requestVote(	int term,
	int candidateId,
	int lastLogIndex,
	int lastLogTerm,
    RequestVoteReply* reply);

Status installSnapshot(int term,
    int leaderId,
    int lastIncludedIndex,
    int lastIncludedTerm,
    char*  data,
    InstallSnapshotReply* reply);

public:
    int peerId;
private:
    std::unique_ptr<RaftRpcs::Stub> stub_;
};






#endif