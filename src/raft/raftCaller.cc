#include "raftCaller.hh"

Status RaftCaller::appenEntries(int term, int leaderId, int prevLogIndex, int prevLogTerm, std::vector<std::shared_ptr<LogEntry>> logs, int leaderCommit, AppendEntriesReply *reply)
{
    AppendEntriesArgs args;

    args.set_term(term);
    args.set_leaderid(leaderId);
    args.set_prevlogindex(prevLogIndex);
    args.set_prevlogterm(prevLogTerm);
    args.set_leadercommit(leaderCommit);
    args.clear_logs();
    for(auto logPtr: logs){
        auto entrySlot = args.add_logs();
        *entrySlot = *logPtr;
    }
    ClientContext context;
    return stub_->appenEntries(&context, args, reply);
}

Status RaftCaller::requestVote(int term, int candidateId, int lastLogIndex, int lastLogTerm, RequestVoteReply *reply)
{
    RequestVoteArgs args;

    args.set_term(term);
    args.set_candidateid(candidateId);
    args.set_lastlogindex(lastLogIndex);
    args.set_lastlogterm(lastLogTerm);
    
    ClientContext context;
    return stub_->requestVote(&context, args, reply);
}

Status RaftCaller::installSnapshot(int term, int leaderId, int lastIncludedIndex, int lastIncludedTerm, char *data, InstallSnapshotReply *reply)
{
    InstallSnapshotArgs args;

    args.set_term(term);
    args.set_leaderid(leaderId);
    args.set_lastincludedindex(lastIncludedIndex);
    args.set_lastincludedterm(lastIncludedTerm);
    args.set_data(data);

    ClientContext context;
    return stub_->installSnapshot(&context, args, reply);
}
