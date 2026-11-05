#pragma once

#include "SOEM_interface/DcTestTypes.h"
#include <algorithm>
#include <cmath>

namespace soem_interface::detail {

// Shared by the diagnostic DC runner and application process-data runner.
// The owner supplies monotonic times, which keeps the arithmetic testable.
inline void recordCycleTiming(DcTestSnapshot& s, int64_t sendTime,
                              int64_t& lastSend, int64_t period) {
    if (lastSend != 0) {
        s.actualCycleNs = std::max<int64_t>(0, sendTime - lastSend);
        if (s.minCycleNs == 0 || s.actualCycleNs < s.minCycleNs)
            s.minCycleNs = s.actualCycleNs;
        s.maxCycleNs = std::max(s.maxCycleNs, s.actualCycleNs);
        s.maxDeviationNs = std::max(s.maxDeviationNs,
                                    std::abs(s.actualCycleNs - period));
    }
    lastSend = sendTime;
}

class DcPhaseController {
    int64_t lastDc_{0};
    double integral_{0};
public:
    void update(DcTestSnapshot& s, int64_t period, bool enabled = true) {
        s.phaseValid = enabled && s.dcTimeValid && s.dcTimeNs > lastDc_;
        s.correctionNs = 0;
        if (!s.phaseValid) {
            s.phaseErrorNs = 0;
            return;
        }
        lastDc_ = s.dcTimeNs;
        int64_t phase = ((s.dcTimeNs % period) - period / 2 - s.shiftNs) % period;
        if (phase > period / 2) phase -= period;
        if (phase < -period / 2) phase += period;
        s.phaseErrorNs = phase;
        integral_ = std::clamp(integral_ + static_cast<double>(phase) * 0.00002,
                              -static_cast<double>(period) * 0.01,
                              static_cast<double>(period) * 0.01);
        const double correction = -(static_cast<double>(phase) * 0.01 + integral_);
        s.correctionNs = static_cast<int64_t>(std::clamp(correction,
            -static_cast<double>(period) * 0.05, static_cast<double>(period) * 0.05));
    }
};

inline void advanceCycleDeadline(int64_t& deadline, int64_t now, int64_t period,
                                 int64_t correction, uint64_t& missedCycles) {
    deadline += period + correction;
    if (deadline <= now) {
        const auto skipped = (now - deadline) / period + 1;
        missedCycles += static_cast<uint64_t>(skipped);
        deadline += skipped * period;
    }
}

} // namespace soem_interface::detail
