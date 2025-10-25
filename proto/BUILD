load("@rules_proto//proto:defs.bzl", "proto_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@rules_proto_grpc_cpp//:defs.bzl", "cpp_grpc_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "chat_proto",
    srcs = ["chat.proto"],
)

cc_proto_library(
    name = "chat_cc_proto",
    deps = [":chat_proto"],
)

cpp_grpc_library(
    name = "chat_grpc",
    protos = [":chat_proto"],
)