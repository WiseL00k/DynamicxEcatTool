#include "Sync0Support.h"
#include "DcRuntime.h"

namespace soem_interface::detail {

bool configureSync0(ecx_contextt& context, DcTestSnapshot& s, std::string& error) {
    if (!ecx_configdc(&context)) {
        error = "No DC-capable slave was found.";
        return false;
    }
    s.referenceSlave = context.grouplist[0].DCnext;
    if (s.referenceSlave == 0 || s.referenceSlave > context.slavecount ||
        !context.slavelist[s.referenceSlave].hasdc) {
        error = "SOEM did not select a valid DC reference slave.";
        return false;
    }
    s.dcSlaveCount = 0;
    for (auto& slave : s.slaves) {
        const auto& item = context.slavelist[slave.address];
        slave.hasDc = item.hasdc != 0;
        slave.propagationDelayNs = item.pdelay;
        if (slave.hasDc) ++s.dcSlaveCount;
    }
    for (auto& slave : s.slaves) if (slave.hasDc) {
        const auto configAddress = context.slavelist[slave.address].configadr;
        slave.cycleNs = s.cycleNs;
        slave.shiftNs = s.shiftNs;
        slave.sync0Requested = true;
        ecx_dcsync0(&context, slave.address, TRUE, s.cycleNs,
                   sync0ShiftForSoem(s.cycleNs, s.shiftNs));
        uint8 activation = 0;
        uint32 cycle = 0;
        const int activationWkc = ecx_FPRD(&context.port, configAddress,
            ECT_REG_DCSYNCACT, sizeof(activation), &activation, EC_TIMEOUTRET);
        const int cycleWkc = ecx_FPRD(&context.port, configAddress,
            ECT_REG_DCCYCLE0, sizeof(cycle), &cycle, EC_TIMEOUTRET);
        slave.sync0Verified = sync0ReadbackMatches(activationWkc, activation,
            cycleWkc, etohl(cycle), s.cycleNs);
        if (!slave.sync0Verified) {
            error = "SYNC0 activation or period readback failed for slave " +
                    std::to_string(slave.address) + ".";
            return false;
        }
    }
    return true;
}

bool disableSync0(ecx_contextt& context, uint16_t slave) {
    if (slave == 0 || slave > context.slavecount) return false;
    ecx_dcsync0(&context, slave, FALSE, 0, 0);
    uint8 activation = 0xff;
    const int received = ecx_FPRD(&context.port, context.slavelist[slave].configadr,
        ECT_REG_DCSYNCACT, sizeof(activation), &activation, EC_TIMEOUTRET);
    return sync0ShutdownVerified(received, activation);
}

} // namespace soem_interface::detail
