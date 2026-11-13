#pragma once

#include "Backend/Canfd/CanfdSession.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

namespace damiao_test {

inline soem_interface::BusScanResult scanFixture(bool canfd)
{
    using namespace soem_interface;
    BusScanResult scan;
    scan.success = scan.mappingReady = scan.activePdoComplete = true;
    scan.slaveCount = 1;
    scan.expectedWorkingCounter = 3;
    SlaveIdentitySnapshot slave;
    slave.position = 1;
    slave.name = canfd ? "ECAT2CANFD" : "MIT";
    slave.productCode = canfd ? 0xAB125 : 0xAB124;
    slave.revision = canfd ? 3 : 2;
    slave.outputBits = slave.inputBits = (canfd ? 185 : 149) * 8;
    slave.state = EC_STATE_PRE_OP;
    scan.slaves.push_back(slave);
    for (auto direction : {PdoDirection::Rx, PdoDirection::Tx}) {
        uint32_t offset = 0;
        auto add = [&](uint16_t index, uint8_t subindex, uint16_t bits) {
            ActivePdoEntry entry;
            entry.slave = 1;
            entry.direction = direction;
            entry.index = index;
            entry.subindex = subindex;
            entry.bitLength = bits;
            entry.bitOffset = offset;
            scan.activePdos.push_back(entry);
            offset += bits;
        };
        const bool output = direction == PdoDirection::Rx;
        if (output) add(0x6040, 0, 32);
        const uint16_t base = output ? 0x7000 : 0x6000;
        for (int field = 0; field < (canfd ? 4 : 2); ++field)
            for (int slot = 1; slot <= 9; ++slot)
                add(base + field, quint8(slot), canfd && field < 2 ? 16 : 64);
        add(output ? (canfd ? 0x7005 : 0x7002) : 0x600C, 0, 8);
        if (!output) add(0x6041, 0, 32);
    }
    return scan;
}

// No socket or timer: tests explicitly advance a bus cycle and inspect the
// bytes that would be submitted, including commands queued before a stop.
class Session final : public Backend::CanfdSession {
public:
    explicit Session(bool canfd = true) : scan(scanFixture(canfd)) {}

    soem_interface::BusScanResult scan;
    std::atomic<int> connects{0}, starts{0}, stops{0}, disconnects{0}, writes{0};
    std::atomic<int> failReadChannel{-1};
    bool identityMismatch{false};

    void setConfigurationField(int channel, int subindex, quint32 value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        values_[channel][subindex] = value;
    }

    soem_interface::BusScanResult connectBus(const std::string&) override
    {
        ++connects;
        return scan;
    }

    void disconnectBus() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++disconnects;
        snapshot_.operational = false;
        snapshot_.finished = true;
    }

    soem_interface::DcStartResult start(const soem_interface::ApplicationRunOptions& options,
                                        soem_interface::PrepareOutputs prepare,
                                        soem_interface::ConsumeInputs consume) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++starts;
        prepare_ = std::move(prepare);
        consume_ = std::move(consume);
        snapshot_ = {};
        snapshot_.operational = true;
        snapshot_.wkc = snapshot_.expectedWkc = 3;
        snapshot_.configured = options.dcEnabled;
        return {true, false, {}};
    }

    void requestStop() override {}

    void stop() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stops;
        snapshot_.operational = false;
        snapshot_.finished = true;
    }

    soem_interface::DcTestSnapshot snapshot() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    soem_interface::SdoTransferResult readSdo(int, quint16 index, quint8 subindex,
                                             int size, void* data) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        soem_interface::SdoTransferResult result;
        quint32 value = 0;
        if (index == 0x1018 && subindex <= 4 && !scan.slaves.empty()) {
            const auto& identity = scan.slaves.front();
            const quint32 values[]{4, identity.vendorId, identity.productCode,
                                   identity.revision, 1};
            value = values[subindex];
            if (identityMismatch && subindex == 2) ++value;
        } else if ((index == 0x2000 || index == 0x2001) && subindex <= 8) {
            if (index - 0x2000 == failReadChannel.load()) {
                result.error = "Injected CAN configuration read failure";
                result.abortCode = 0x06020000;
                return result;
            }
            value = values_[index - 0x2000][subindex];
        } else {
            result.error = "Unexpected SDO read in test session";
            result.abortCode = 0x06020000;
            return result;
        }
        const auto bytes = Backend::canfd::writeLe(value, size);
        std::memcpy(data, bytes.data(), size_t(size));
        result.success = true;
        result.wkc = 1;
        result.actualSize = size;
        return result;
    }

    soem_interface::SdoTransferResult writeSdo(int, quint16 index, quint8 subindex,
                                              const std::vector<quint8>& bytes) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++writes;
        soem_interface::SdoTransferResult result;
        if ((index != 0x2000 && index != 0x2001) || subindex < 1 || subindex > 6) {
            result.error = "Unexpected SDO write in test session";
            return result;
        }
        auto& channel = values_[index - 0x2000];
        channel[subindex] = Backend::canfd::readLe(bytes.data(), int(bytes.size()));
        channel[7] = subindex == 6 ? 0 : 1;
        if (subindex == 6) channel[6] = 0;
        result.success = true;
        result.wkc = 1;
        result.actualSize = int(bytes.size());
        return result;
    }

    std::vector<soem_interface::SlaveProcessImage> cycle(int wkc = 3)
    {
        soem_interface::PrepareOutputs prepare;
        soem_interface::ConsumeInputs consume;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            prepare = prepare_;
            consume = consume_;
        }
        std::vector<soem_interface::SlaveProcessImage> images;
        for (const auto& slave : scan.slaves) {
            soem_interface::SlaveProcessImage image;
            image.position = slave.position;
            image.outputs.resize((slave.outputBits + 7) / 8);
            image.inputs.resize((slave.inputBits + 7) / 8);
            images.push_back(std::move(image));
        }
        if (prepare) prepare(images);
        if (consume) {
            soem_interface::ApplicationCycle sample;
            sample.sequence = ++sequence_;
            sample.hostTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            sample.workingCounter = wkc;
            sample.expectedWorkingCounter = 3;
            sample.slaves = images;
            consume(sample);
        }
        return images;
    }

private:
    mutable std::mutex mutex_;
    soem_interface::DcTestSnapshot snapshot_;
    soem_interface::PrepareOutputs prepare_;
    soem_interface::ConsumeInputs consume_;
    quint32 values_[2][9]{{8, 1, 0, 1000000, 5000000, 5, 0, 0, 0},
                          {8, 1, 0, 1000000, 5000000, 5, 0, 0, 0}};
    uint64_t sequence_{0};
};

} // namespace damiao_test
