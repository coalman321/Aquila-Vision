# BUILD file overlaid onto the @mwlib repository (MW-Lib, fetched at a pinned tag in MODULE.bazel),
# which has no Bazel files of its own. Only the chassis proxy protocol is built from it.
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "chassis_proxy_proto",
    srcs = glob(["src/main/proto/chassis_proxy/**/*.proto"]),
    strip_import_prefix = "src/main/proto",
)

cc_proto_library(
    name = "chassis_proxy_cc_proto",
    deps = [":chassis_proxy_proto"],
)

# Golden Envelope frames that MW-Lib's Java tests also check against, so each end is tested on what the
# other sends: .binpb as protoc (the C++ library) encodes them, .quickbuf.binpb as MW-Lib encodes them,
# and the .txtpb each was made from.
filegroup(
    name = "chassis_proxy_fixtures",
    srcs = glob(["src/test/resources/chassis_proxy/*.binpb", "src/test/resources/chassis_proxy/*.txtpb"]),
)
