#pragma once

#include <QObject>
#include <atomic>
#include <memory>

namespace soem_interface {
class EcatMasterBus;
}

namespace Backend {

class EthercatTestWorker : public QObject
{
    Q_OBJECT

public:
    explicit EthercatTestWorker(std::shared_ptr<soem_interface::EcatMasterBus> master);

public slots:
    void run();
    void stop();

signals:
    void logUpdated(const QString& line);
    void finished();

private:
    void testLogRefresh();

    std::atomic<bool> running_{false};
    std::shared_ptr<soem_interface::EcatMasterBus> master_;
};

class EthercatWorker : public QObject
{
    Q_OBJECT

public:
    explicit EthercatWorker(std::shared_ptr<soem_interface::EcatMasterBus> master);

public slots:
    void run();
    void stop();

signals:
    void setDeviceOnlineStatus(const QString& name, const bool& status);
    void logUpdated(const QString& line);
    void finished();

private:
    void pdoLogRefresh();

    std::atomic<bool> running_{false};
    std::shared_ptr<soem_interface::EcatMasterBus> master_;
};

} // namespace Backend
