#include "proto/chat.grpc.pb.h"
#include "proto/chat.pb.h"
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

std::mutex g_mu;
std::vector<ChatSession *> g_clients;

class ChatSession : public grpc::ServerBidiReactor<ChatMessage, ChatMessage> {
public:
  ChatSession() { StartRead(&incoming_); }

  void OnReadDone(bool ok) override {
    if (!ok) {
      FinishSession();
      return;
    }
    {
      std::lock_guard<std::mutex> lock(g_mu);
      for (auto *client : g_clients) {
        if (client == this)
          continue;
        client->Send(incoming_);
      }
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
};

class ChatServiceImpl final : public ChatService::CallbackService {
public:
  grpc::ServerBidiReactor<ChatMessage, ChatMessage> *
  Chat(CallbackServerContext *context) override { // Corrected context type
    auto *session = new ChatSession();
    {
      std::lock_guard<std::mutex> lock(g_mu);
      g_clients.push_back(session);
    }
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