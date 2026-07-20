#pragma once

#include "Backend/Explorer/EsiRepository.h"
#include "Backend/Explorer/ExplorerModels.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QThreadPool>

#include <memory>
#include <string>
#include <vector>

namespace soem_interface {
struct ActivePdoEntry;
struct BusScanResult;
struct BusStateResult;
}

namespace Backend {

class BusSessionCoordinator;
class EthercatMasterController;

class EthercatExplorerController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool scanned READ scanned NOTIFY scannedChanged)
    Q_PROPERTY(int currentState READ currentState NOTIFY currentStateChanged)
    Q_PROPERTY(int slaveCount READ slaveCount NOTIFY slaveCountChanged)
    Q_PROPERTY(bool mappingReady READ mappingReady NOTIFY mappingReadyChanged)
    Q_PROPERTY(bool allEsiTrusted READ allEsiTrusted NOTIFY allEsiTrustedChanged)
    Q_PROPERTY(QString esiDirectory READ esiDirectory WRITE setEsiDirectory NOTIFY esiDirectoryChanged)
    Q_PROPERTY(int selectedSlaveAddress READ selectedSlaveAddress NOTIFY selectedSlaveAddressChanged)
    Q_PROPERTY(QAbstractItemModel* slavesModel READ slavesModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* pdoEntriesModel READ pdoEntriesModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* pdoVariableGroupsModel READ pdoVariableGroupsModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* pdoMappingsModel READ pdoMappingsModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* objectDictionaryModel READ objectDictionaryModel CONSTANT)
    Q_PROPERTY(QStringList logs READ logs NOTIFY logsChanged)

public:
    enum class Status {
        Idle,
        Scanning,
        Ready,
        Failed,
        Resetting
    };
    Q_ENUM(Status)

    enum AlState {
        Init = 0x01,
        PreOp = 0x02,
        SafeOp = 0x04,
        Op = 0x08
    };
    Q_ENUM(AlState)

    EthercatExplorerController(BusSessionCoordinator& sessionCoordinator,
                               EthercatMasterController& masterController,
                               QObject* parent = nullptr);
    ~EthercatExplorerController() override;

    Status status() const;
    QString statusText() const;
    bool busy() const;
    bool scanned() const;
    int currentState() const;
    int slaveCount() const;
    bool mappingReady() const;
    bool allEsiTrusted() const;
    QString esiDirectory() const;
    int selectedSlaveAddress() const;
    QStringList logs() const;

    QAbstractItemModel* slavesModel();
    QAbstractItemModel* pdoEntriesModel();
    QAbstractItemModel* pdoVariableGroupsModel();
    QAbstractItemModel* pdoMappingsModel();
    QAbstractItemModel* objectDictionaryModel();

    void setNicName(const std::string& nicName);
    void setEsiDirectory(const QString& directory);

    Q_INVOKABLE void scan();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void requestState(int state);
    Q_INVOKABLE void selectSlave(int address);
    Q_INVOKABLE void selectPdoArrayElement(const QString& groupId,
                                           int elementIndex);
    Q_INVOKABLE void writePdoValue(const QString& stableId, const QVariant& value);
    Q_INVOKABLE void readSdoValue(const QString& stableId);
    Q_INVOKABLE void writeSdoValue(const QString& stableId, const QVariant& value);

signals:
    void statusChanged();
    void busyChanged();
    void scannedChanged();
    void currentStateChanged();
    void slaveCountChanged();
    void mappingReadyChanged();
    void allEsiTrustedChanged();
    void esiDirectoryChanged();
    void selectedSlaveAddressChanged();
    void logsChanged();
    void logAppended(const QString& line);
    void errorOccurred(const QString& message);
    void sessionReleased();

private:
    struct SlaveRuntime
    {
        explorer::EsiMatchResult match;
        QVector<explorer::PdoMapping> mappings;
        QVector<explorer::ObjectDictionaryEntry> objects;
        int outputBits{0};
        int inputBits{0};
    };

    void setStatus(Status status);
    void setBusy(bool busy);
    void appendLog(const QString& line);
    void clearModels();
    void completeScan(quint64 generation,
                      soem_interface::BusScanResult result,
                      QVector<explorer::EsiDevice> devices,
                      QVector<explorer::ParseDiagnostic> diagnostics);
    void buildRuntime(const soem_interface::BusScanResult& result);
    void completeStateRequest(quint64 generation,
                              int requestedState,
                              soem_interface::BusStateResult result);
    void performReset();
    void completeReset(quint64 generation);
    void refreshProcessValues();
    void loadOnlineObjectDictionary(quint16 address);
    void refreshSelectedModels();
    void applyOnlineAccessForState();
    void updateSlaveStates(const soem_interface::BusStateResult& result);
    bool validateStateRequest(int state, QString& errorMessage) const;

    static QString stateText(int state);
    static QString coeDataTypeName(quint16 dataType, int bitLength);
    static explorer::AccessMode accessModeFromCoe(quint16 objectAccess, int state);
    static QVector<explorer::PdoMapping> mergeMappings(
        quint16 address,
        const std::vector<soem_interface::ActivePdoEntry>& activeEntries,
        const explorer::EsiMatchResult& match);
    static QVector<explorer::ObjectDictionaryEntry> flattenedObjects(
        const QVector<explorer::ObjectDictionaryEntry>& objects);

    BusSessionCoordinator& sessionCoordinator_;
    EthercatMasterController& masterController_;

    Status status_{Status::Idle};
    bool busy_{false};
    bool scanned_{false};
    int currentState_{0};
    int slaveCount_{0};
    bool mappingReady_{false};
    bool allEsiTrusted_{false};
    int selectedSlaveAddress_{0};
    QString esiDirectory_;
    std::string nicName_;
    QStringList logs_;
    quint64 generation_{0};
    bool resetPending_{false};

    explorer::EsiRepository repository_;
    explorer::ExplorerSlaveListModel slavesModel_;
    explorer::ExplorerPdoVariableModel pdoEntriesModel_;
    explorer::ExplorerPdoVariableGroupModel pdoVariableGroupsModel_;
    explorer::ExplorerPdoMappingModel pdoMappingsModel_;
    explorer::ExplorerObjectDictionaryModel objectDictionaryModel_;
    QHash<quint16, SlaveRuntime> runtimes_;

    QTimer refreshTimer_;
    QThreadPool workerPool_;
    bool wkcFaultReported_{false};
    std::shared_ptr<QMutex> sdoMutex_;
};

} // namespace Backend
