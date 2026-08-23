#ifndef ETHERCATBACKEND_H
#define ETHERCATBACKEND_H

#include "Backend/Ethercat/BusSessionCoordinator.h"
#include "Backend/Ethercat/EthercatMasterController.h"
#include "Backend/Ethercat/MitSlaveController.h"
#include "Backend/Explorer/EthercatExplorerController.h"
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
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY sessionChanged)
    Q_PROPERTY(QString sessionMode READ sessionMode NOTIFY sessionChanged)
    Q_PROPERTY(EthercatExplorerController* busExplorer READ busExplorer CONSTANT)
    Q_PROPERTY(DeviceStatusModel* deviceStatusList READ motorStatusList CONSTANT)

public:
    explicit EthercatBackend(QObject* parent = nullptr);
    ~EthercatBackend() override;

    QStringList nicList() const;
    bool connected() const;
    int slaveCount() const;
    bool sessionActive() const;
    QString sessionMode() const;
    EthercatExplorerController* busExplorer();
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
    void sessionChanged();

private:
    void refreshNics();
    bool validateSelectedNic() const;
    bool ensureConfigFileSelected();
    bool ensureMonitorStopped();
    bool acquireSession(BusSessionCoordinator::Mode mode);
    bool isSession(BusSessionCoordinator::Mode mode) const;
    void clearMotorStatusList();
    void resetConnectionState(BusSessionCoordinator::Mode mode);
    void failConnection(soem_interface::error::SoemInterfaceErrorCode errorCode, BusSessionCoordinator::Mode mode);
    void updateConnectionState(bool connected, int slaveCount);
    void connectServices();
    void emitStartFailure(const MasterStartResult& result, BusSessionCoordinator::Mode mode);

    QStringList nicList_;
    DeviceStatusModel deviceModel_;
    bool connected_{false};
    int slaveCount_{0};

    NetworkAdapterService adapterService_;
    BusSessionCoordinator sessionCoordinator_;
    EthercatMasterController masterController_;
    EthercatExplorerController busExplorer_;
    EthercatMonitorController monitorController_;
    FlashService flashService_;
    MitSlaveController mitSlaveController_;
    std::string configFilePath_;
    std::string nicName_;
};

} // namespace Backend

#endif // ETHERCATBACKEND_H
