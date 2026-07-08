#ifndef ETHERCATBACKEND_H
#define ETHERCATBACKEND_H

#include "Backend/Ethercat/EthercatMasterController.h"
#include "Backend/Ethercat/MitSlaveController.h"
#include "Backend/Flash/FlashService.h"
#include "Backend/Models/DeviceStatusModel.h"
#include "Backend/Monitor/EthercatMonitorController.h"
#include "Backend/Network/NetworkAdapterService.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <string>

namespace Backend {

class EthercatBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList nicList READ nicList NOTIFY nicListChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int slaveCount READ slaveCount NOTIFY slaveCountChanged)
    Q_PROPERTY(DeviceStatusModel* deviceStatusList READ motorStatusList CONSTANT)

public:
    explicit EthercatBackend(QObject* parent = nullptr);
    ~EthercatBackend() override;

    QStringList nicList() const;
    bool connected() const;
    int slaveCount() const;
    DeviceStatusModel* motorStatusList()
    {
        return &deviceModel_;
    }

public slots:
    void changedSelectedNic(const int& nicIndex);
    QString changeConfigFilePath(const QString& config_file_path);
    void startTest();
    void stopTest();
    void startCommunication();
    void stopCommunication();
    bool setDeviceOnlineStatus(const QString& name, const bool& status);
    void enterPreOpAll();
    void exitPreOpAll();
    bool applySDOConfigsQml(const QVariantList& list);
    void refreshNicsAsync();
    void flashEEprom(int slaveId, const QString& filePath);
    void flashFirmware(int slaveId, const QString& filePath);
    void enterMitSlaveDebugMode();
    void exitMitSlaveDebugMode();
    void enableMitSlaveMotors();
    void disableMitSlaveMotors();
    void sendMitFrameQml(int canBus, int canId, const QVariantList& data);
    void clearMitFrameQml(int canBus, int canId);

signals:
    void nicListChanged();
    void connectedChanged();
    void slaveCountChanged();
    void logUpdated(const QString& line);
    void logAppend(const QString& line);
    void connectedUpdated(const int connected_status);
    void motorStatusListChanged();
    void soemErrorOccurred(QString message);
    void flashProgress(QString type, int percent);
    void flashFinished(QString type, bool success, QString msg);

private:
    enum class ConnectionMode {
        Idle,
        Test,
        Communication,
        PreOp,
        MitDebug
    };

    void refreshNics();
    bool validateSelectedNic() const;
    bool ensureConfigFileSelected();
    bool ensureMonitorStopped();
    void clearMotorStatusList();
    void resetConnectionState();
    void failConnection(soem_interface::error::SoemInterfaceErrorCode errorCode);
    void updateConnectionState(bool connected, int slaveCount);
    void connectServices();
    void emitStartFailure(const MasterStartResult& result);

    QStringList nicList_;
    DeviceStatusModel deviceModel_;
    bool connected_{false};
    int slaveCount_{0};

    NetworkAdapterService adapterService_;
    EthercatMasterController masterController_;
    EthercatMonitorController monitorController_;
    FlashService flashService_;
    MitSlaveController mitSlaveController_;
    std::string configFilePath_;
    std::string nicName_;
    ConnectionMode mode_{ConnectionMode::Idle};
};

} // namespace Backend

#endif // ETHERCATBACKEND_H
