#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace soem_interface {

struct ApplicationRunOptions {
    int64_t cycleUs{2000};
    bool dcEnabled{false};
    int64_t shiftUs{0};
};

struct SlaveProcessImage {
    uint16_t position{0};
    std::vector<uint8_t> outputs;
    std::vector<uint8_t> inputs;
};

struct ApplicationCycle {
    uint64_t sequence{0};
    int64_t hostTimeNs{0}; // std::chrono::steady_clock, nanoseconds
    int workingCounter{0};
    int expectedWorkingCounter{0};
    int64_t dcTimeNs{0};
    std::vector<SlaveProcessImage> slaves;
};

// Called on the process-data thread. Only update/copy memory; do not perform
// SDO, lifecycle operations, UI calls or wait for another thread. Image sizes
// and slave positions are fixed by the active mapping and must not be changed.
using PrepareOutputs = std::function<void(std::vector<SlaveProcessImage>&)>;
using ConsumeInputs = std::function<void(const ApplicationCycle&)>;

struct SdoTransferResult {
    bool success{false};
    int wkc{0};
    int actualSize{0};
    uint32_t abortCode{0};
    std::string error;
};

} // namespace soem_interface
