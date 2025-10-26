#include "proto/chat.grpc.pb.h"
#include "proto/chat.pb.h"
#include <algorithm>
#include <grpcpp/grpcpp.h>

#include <iostream>
#include <mutex>

using chat::ChatMessage;
using chat::ChatService;
using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;

// Forward declare
class ChatSession;

class ChatGroup {
public:
  ChatGroup(std::string name) : name_(std::move(name)) {}

  void AddMember(ChatSession *session) {
    std::lock_guard<std::mutex> lock(mu_);
    members_.push_back(session);
  }

  void RemoveMember(ChatSession *session) {
    std::lock_guard<std::mutex> lock(mu_);
    members_.erase(std::remove(members_.begin(), members_.end(), session),
                   members_.end());
  }

  void Broadcast(const ChatMessage &msg);

  const std::string &name() const { return name_; }

private:
  std::string name_;
  std::mutex mu_;
  std::vector<ChatSession *> members_;
};

class GroupManager {
public:
  static GroupManager &GetInstance() {
    static GroupManager instance;
    return instance;
  }

  ChatGroup *GetOrCreateGroup(const std::string &group_name) {
    std::lock_guard<std::mutex> lock(mu_);
    if (groups_.find(group_name) == groups_.end()) {
      groups_[group_name] = std::make_unique<ChatGroup>(group_name);
    }
    return groups_[group_name].get();
  }

private:
  GroupManager() = default;
  std::mutex mu_;
  std::unordered_map<std::string, std::unique_ptr<ChatGroup>> groups_;
};

class ChatSession : public grpc::ServerBidiReactor<ChatMessage, ChatMessage> {
public:
  ChatSession() { StartRead(&incoming_); }

  void OnReadDone(bool ok) override {
    if (!ok) {
      if (current_group_) {
        current_group_->RemoveMember(this);
      }
      FinishSession();
      return;
    }
    if (!current_group_ && !incoming_.group_name().empty()) {
      current_group_ =
          GroupManager::GetInstance().GetOrCreateGroup(incoming_.group_name());
      current_group_->AddMember(this);
      std::cerr << "Client joined group: " << incoming_.group_name()
                << std::endl;
    }

    if (current_group_ && incoming_.group_name() == current_group_->name()) {
      current_group_->Broadcast(incoming_);
    } else {
      std::cerr << "Error: Message from session not in target group."
                << std::endl;
    }
    StartRead(&incoming_);
  }

  void OnWriteDone(bool ok) override {
    if (!ok) {
      FinishSession();
      return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (!outbox_.empty()) {
      auto msg = outbox_.front();
      outbox_.erase(outbox_.begin());
      StartWrite(&msg);
    } else {
      writing_ = false;
    }
  }

  void OnDone() override {
    std::cerr << "Client disconnected (cancelled)" << std::endl;
  }

  void Send(const ChatMessage &msg) {
    std::lock_guard<std::mutex> lock(mu_);
    if (writing_) {
      outbox_.push_back(msg);
    } else {
      writing_ = true;
      StartWrite(&msg);
    }
  }

private:
  void FinishSession() { Finish(grpc::Status::OK); }

  ChatMessage incoming_;
  std::mutex mu_;
  std::vector<ChatMessage> outbox_;
  bool writing_ = false;
  // TODO: Multiple chat groups
  ChatGroup *current_group_ = nullptr;
};

void ChatGroup::Broadcast(const ChatMessage &msg) {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto *member : members_) {
    // TODO: Do not send to the sender
    member->Send(msg);
  }
}

class ChatServiceImpl final : public ChatService::CallbackService {
public:
  grpc::ServerUnaryReactor* CreateGroup(
    CallbackServerContext* context,
    const chat::CreateGroupRequest* request,
    chat::CreateGroupResponse* response
  ) override {
    auto* reactor = context->DefaultReactor();
    reactor->Finish(Status::OK);
    return reactor;
  }

  grpc::ServerBidiReactor<ChatMessage, ChatMessage> *
  Chat(CallbackServerContext *context) override {
    auto *session = new ChatSession();
    return session;
  }
};

int main(int argc, char **argv) {
  std::string server_address("0.0.0.0:50051");
  ChatServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  builder.SetResourceQuota(grpc::ResourceQuota().SetMaxThreads(8));

  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Chat server (Reactor based) listening on " << server_address
            << std::endl;
  server->Wait();
  return 0;
}