#include "EthercatMonitorWorker.h"

#include "SOEM_interface/EcatMasterBus.h"
#include "SOEM_interface/EcatSlaveBase.h"

#include <QThread>
#include <bitset>

namespace Backend {
namespace {

QString statuswordLogLine(int address, const std::string& name, uint32_t raw)
{
    const std::bitset<32> bits(raw);
    return QStringLiteral("\nSlave %1 (%2) \nStatusword: %3")
        .arg(address)
        .arg(QString::fromStdString(name))
        .arg(QString::fromStdString(bits.to_string()));
}

void appendBytes(QString& line, const std::vector<uint8_t>& bytes)
{
    for (uint8_t value : bytes) {
        line += QStringLiteral(" %1").arg(value, 2, 16, QChar('0'));
    }
}

} // namespace

EthercatTestWorker::EthercatTestWorker(std::shared_ptr<soem_interface::EcatMasterBus> master)
    : master_(std::move(master))
{}

void EthercatTestWorker::run()
{
    running_ = true;

    while (running_) {
        testLogRefresh();
        QThread::msleep(20);
    }

    emit finished();
}

void EthercatTestWorker::stop()
{
    running_ = false;
}

void EthercatTestWorker::testLogRefresh()
{
    if (!master_) {
        return;
    }

    static int cycle = 0;
    cycle %= 1000000;

    const auto snapshot = master_->processDataSnapshot();
    QString line = QString::asprintf(
        "Processdata cycle %5d , Wck %3d, DCtime %12lld, O:",
        cycle++,
        snapshot.workingCounter,
        static_cast<long long>(snapshot.dcTime));

    appendBytes(line, snapshot.outputPreview);
    line += QStringLiteral(" I:");
    appendBytes(line, snapshot.inputPreview);

    emit logUpdated(line);
}

EthercatWorker::EthercatWorker(std::shared_ptr<soem_interface::EcatMasterBus> master)
    : master_(std::move(master))
{}

void EthercatWorker::run()
{
    running_ = true;

    while (running_) {
        pdoLogRefresh();
        QThread::msleep(20);
    }

    emit finished();
}

void EthercatWorker::stop()
{
    running_ = false;
}

void EthercatWorker::pdoLogRefresh()
{
    if (!master_) {
        return;
    }

    QString line;
    for (const auto& slave : master_->registeredSlaves()) {
        if (!slave) {
            continue;
        }

        line += statuswordLogLine(
            static_cast<int>(slave->getAddress()),
            slave->getName(),
            slave->statuswordRaw());

        for (const auto& status : slave->collectDeviceOnlineStatuses()) {
            emit setDeviceOnlineStatus(QString::fromStdString(status.name), status.online);
        }
    }

    emit logUpdated(line);
}

} // namespace Backend
#include <utility>


