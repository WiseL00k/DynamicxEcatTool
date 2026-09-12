#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace soem_interface {

struct DcTestOptions { int64_t cycleUs{2000}; int64_t shiftUs{0}; };
struct DcStartResult { bool success{false}; bool cancelled{false}; std::string error; };
struct DcSlaveSnapshot {
    uint16_t address{0}; std::string name; bool hasDc{false};
    bool sync0Requested{false}; bool sync0Verified{false};
    bool shutdownAttempted{false}; bool shutdownVerified{false};
    uint32_t cycleNs{0}; int32_t shiftNs{0}; int32_t propagationDelayNs{0};
    uint16_t state{0}; uint16_t alStatusCode{0}; std::string alStatusText;
};
struct DcTestSnapshot {
    bool configured{false}; bool operational{false}; bool dcTimeValid{false};
    bool phaseValid{false}; bool finished{false}; bool cancelled{false};
    uint16_t referenceSlave{0}; int dcSlaveCount{0};
    uint32_t cycleNs{0}; int32_t shiftNs{0};
    int wkc{0}; int expectedWkc{0}; int64_t dcTimeNs{0};
    uint64_t cycleCount{0}; uint64_t wkcErrorCount{0}; uint64_t missedCycles{0};
    int64_t actualCycleNs{0}; int64_t minCycleNs{0}; int64_t maxCycleNs{0};
    int64_t maxDeviationNs{0}; int64_t phaseErrorNs{0}; int64_t correctionNs{0};
    std::string error; std::string cleanupWarning; std::vector<DcSlaveSnapshot> slaves;
};

} // namespace soem_interface
