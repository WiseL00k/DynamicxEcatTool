#ifndef ETHERCATBACKEND_H
#define ETHERCATBACKEND_H

#include "SOEM_interface/EcatSlaveBase.h"
#include "SOEM_interface/SoemUtils.h"
#include "MotorStatusModel.h"
#include <QObject>
#include <QThread>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtConcurrent>
#include <memory>

namespace soem_interface {
class EcatMasterBus;
}

namespace Backend {

using namespace soem_interface::error;

class EthercatTestWorker : public QObject
{
    Q_OBJECT
public:
    EthercatTestWorker(soem_interface::EcatMasterBus* master)
        : master_(master)
    {}

public slots:
    void run()
    {
        running_ = true;

        while (running_)
        {
            testLogRefresh();
            QThread::msleep(20);
        }

        emit finished();
    }

    void stop()
    {
        running_ = false;
    }

signals:
    void logUpdated(const QString&);
    void finished();

private:
    void testLogRefresh();
    std::atomic<bool> running_{false};
    soem_interface::EcatMasterBus* master_;
};

class EthercatWorker : public QObject
{
    Q_OBJECT
public:
    EthercatWorker(soem_interface::EcatMasterBus* master)
        : master_(master)
    {}

public slots:
    void run()
    {
        running_ = true;

        while (running_)
        {
            pdoLogRefresh();
            QThread::msleep(20);
        }

        emit finished();
    }

    void stop()
    {
        running_ = false;
    }

signals:
    void setMotorOnlineStatus(const QString& motorName, const bool& status);
    void logUpdated(const QString&);
    void finished();

private:
    void pdoLogRefresh();
    std::atomic<bool> running_{false};
    soem_interface::EcatMasterBus* master_;
};



class EthercatBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList nicList READ nicList NOTIFY nicListChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int slaveCount READ slaveCount NOTIFY slaveCountChanged)
    Q_PROPERTY(MotorStatusModel* motorStatusList READ motorStatusList CONSTANT)

public:
    explicit EthercatBackend(QObject* parent = nullptr);
    ~EthercatBackend();
    QStringList nicList() const;
    bool connected() const;
    int slaveCount() const;
    MotorStatusModel* motorStatusList()
    {
        return &motorModel_;
    }

public slots:
    void changedSelectedNic(const int& nicIndex);
    QString changeConfigFilePath(const QString &config_file_path);
    void startTest();
    void stopTest();
    void startCommunication();
    void stopCommunication();
    bool setMotorOnlineStatus(const QString& motorName, const bool& status);
    void enterPreOpAll();
    void exitPreOpAll();
    bool applySDOConfigsQml(const QVariantList& list);
    void refreshNicsAsync();

signals:
    void nicListChanged();
    void connectedChanged();
    void slaveCountChanged();
    void logUpdated(const QString &line);
    void logAppend(const QString &line);
    void connectedUpdated(const int connected_status);
    void motorStatusListChanged();
    void soemErrorOccurred(QString message);

private:
    void refreshNics();
    void testLogRefresh();
    bool parseSlaveInfo();
    void clearMotorStatusList();
    std::vector<soem_interface::SDOConfig> parseSDOConfigs(const QVariantList& list);

    QStringList nicList_;
    // QVariantList motorStatusList_;
    MotorStatusModel motorModel_;
    bool connected_{false};
    int slaveCount_{0};

    std::shared_ptr<soem_interface::EcatMasterBus> master_;

    EthercatTestWorker* testWorker_{};
    EthercatWorker* worker_{};
    QThread* workerThread_;

    std::string configFilePath_, nicName_;
    std::vector<soem_interface::SoemUtils::AdapterInfo> adapters;
};
}
#endif
