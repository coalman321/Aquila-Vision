#pragma once

#include <string>

namespace cuda_test {

// True when this process can run the build's CUDA kernels: a device is visible and the build has a
// kernel image for its architecture (every cuda_library shares the --@rules_cuda//cuda:archs
// setting, so a probe kernel compiled here stands in for all of them). Otherwise false, with the
// reason in *reason, for a test to report as it skips. With CUDA_TEST_REQUIRE_GPU set (and not "0")
// it instead prints the reason and exits the test with status 1.
bool GpuAvailable(std::string* reason);

}  // namespace cuda_test
