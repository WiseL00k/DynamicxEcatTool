#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QThread;

namespace soem_interface {
class EcatMasterBus;
}

namespace Backend {

class EthercatTestWorker;
class EthercatWorker;

class EthercatMonitorController : public QObject
{
    Q_OBJECT

public:
    explicit EthercatMonitorController(QObject* parent = nullptr);
    ~EthercatMonitorController() override;

    void startTest(std::shared_ptr<soem_interface::EcatMasterBus> master);
    void startCommunication(std::shared_ptr<soem_interface::EcatMasterBus> master);
    bool stop();
    bool isRunning() const;

signals:
    void logUpdated(const QString& line);
    void setDeviceOnlineStatus(const QString& name, const bool& status);
    void communicationMonitorStopped();

private:
    EthercatTestWorker* testWorker_{nullptr};
    EthercatWorker* worker_{nullptr};
    QThread* workerThread_{nullptr};
};

} // namespace Backend
