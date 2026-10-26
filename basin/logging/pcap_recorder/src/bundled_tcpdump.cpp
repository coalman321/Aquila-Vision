#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "pcap_recorder/pcap_recorder_params.hpp"
#include "rules_cc/cc/runfiles/runfiles.h"

using rules_cc::cc::runfiles::Runfiles;

namespace basin::pcap_recorder {

std::string bundledTcpdumpPath(std::string* error) {
    // Without RUNFILES_DIR in the environment (the binary run straight out of bazel-bin, say), the runfiles library
    // looks next to argv0; /proc/self/exe stands in for it, since a library doesn't get argv.
    std::error_code ignored;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ignored);

    std::unique_ptr<Runfiles> runfiles(Runfiles::Create(exe.string(), BAZEL_CURRENT_REPOSITORY, error));
    if (!runfiles) {
        return "";
    }
    // Set by the BUILD file to $(rlocationpath @tcpdump//:tcpdump): already a canonical path, so no repo mapping
    // is needed to resolve it
    const std::string path = runfiles->Rlocation(PCAP_RECORDER_TCPDUMP_RLOCATION);
    if (path.empty() || !std::filesystem::exists(path)) {
        *error = std::string(PCAP_RECORDER_TCPDUMP_RLOCATION) + " is not in the runfiles";
        return "";
    }
    return std::filesystem::absolute(path).string();
}

}  // namespace basin::pcap_recorder
