load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

cc_binary(
    name = "chat-server",
    srcs = ["chat.cpp"],
    deps = [
        "//proto:chat_grpc",
        "//proto:chat_cc_proto",
    ]
)