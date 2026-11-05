#pragma once

#include "CanfdProtocol.h"
#include <atomic>
#include <memory>

namespace Backend {

class CanfdSession
{
public:
    virtual ~CanfdSession() = default;
    virtual soem_interface::BusScanResult connectBus(const std::string& nic) = 0;
    virtual void disconnectBus() = 0;
    virtual soem_interface::DcStartResult start(const soem_interface::ApplicationRunOptions& options,
                                               soem_interface::PrepareOutputs prepare,
                                               soem_interface::ConsumeInputs consume) = 0;
    virtual void requestStop() = 0;
    virtual void stop() = 0;
    virtual soem_interface::DcTestSnapshot snapshot() const = 0;
    virtual soem_interface::SdoTransferResult readSdo(int slave, quint16 index, quint8 subindex,
                                                    int size, void* data) = 0;
    virtual soem_interface::SdoTransferResult writeSdo(int slave, quint16 index, quint8 subindex,
                                                     const std::vector<quint8>& data) = 0;
};

struct CanfdConfigResult {
    bool success{false};
    QString error;
    std::vector<canfd::ChannelConfig> configs;
};

std::unique_ptr<CanfdSession> makeCanfdSession();
bool verifyCanfdIdentity(CanfdSession& session, int slave, QString& error);
CanfdConfigResult readCanfdConfiguration(CanfdSession& session, const std::vector<int>& slaves,
                                        const std::atomic<bool>& cancel);
CanfdConfigResult applyCanfdConfiguration(CanfdSession& session,
                                         const std::vector<canfd::ChannelConfig>& configs,
                                         const std::atomic<bool>& cancel);

} // namespace Backend
