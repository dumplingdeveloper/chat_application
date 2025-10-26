#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "proto/chat.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <thread>

using chat::ChatMessage;
using chat::ChatService;
using grpc::ClientContext;
using grpc::ClientReaderWriter;

ABSL_FLAG(std::string, server_address, "localhost:50051", "Server address");
ABSL_FLAG(std::string, user_name, "anonymous", "User name");
ABSL_FLAG(std::string, group_name, "general", "Chat group name");

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
  auto channel = grpc::CreateChannel(absl::GetFlag(FLAGS_server_address),
                                     grpc::InsecureChannelCredentials());
  std::unique_ptr<ChatService::Stub> stub = ChatService::NewStub(channel);

  ClientContext ctx;
  std::shared_ptr<ClientReaderWriter<ChatMessage, ChatMessage>> stream(
      stub->Chat(&ctx));

  std::thread reader([stream]() {
    ChatMessage msg;
    while (stream->Read(&msg)) {
      std::cout << msg.user() << ": " << msg.text() << std::endl;
    }
  });

  ChatMessage msg;
  msg.set_user(absl::GetFlag(FLAGS_user_name));
    msg.set_group_name(absl::GetFlag(FLAGS_group_name));
  while (true) {
    std::string text;
    std::getline(std::cin, text);
    if (text == "/quit")
      break;
    msg.set_text(text);
    msg.set_timestamp_ms(time(nullptr));
    stream->Write(msg);
  }

  stream->WritesDone();
  reader.join();
  grpc::Status status = stream->Finish();
  std::cout << "Disconnected: " << status.error_message() << std::endl;
}