#pragma once

#include "CanfdSession.h"
#include "CanFrameModel.h"
#include <QObject>
#include <QThreadPool>
#include <QTimer>

namespace Backend {
class BusSessionCoordinator;

class Ecat2CanfdController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY resultsChanged)
    Q_PROPERTY(int slaveCount READ slaveCount NOTIFY resultsChanged)
    Q_PROPERTY(int selectedSlave READ selectedSlave NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList slaves READ slaves NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList channelConfigs READ channelConfigs NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY resultsChanged)
    Q_PROPERTY(QAbstractItemModel* frames READ frames CONSTANT)
    Q_PROPERTY(QVariantMap pdoSnapshot READ pdoSnapshot NOTIFY resultsChanged)
    Q_PROPERTY(bool periodicSending READ periodicSending NOTIFY stateChanged)
    Q_PROPERTY(int periodicIntervalUs READ periodicIntervalUs NOTIFY stateChanged)
    Q_PROPERTY(qulonglong receivedCount READ receivedCount NOTIFY resultsChanged)
    Q_PROPERTY(qulonglong droppedCount READ droppedCount NOTIFY resultsChanged)
public:
    explicit Ecat2CanfdController(BusSessionCoordinator& coordinator, QObject* parent = nullptr);
    Ecat2CanfdController(BusSessionCoordinator& coordinator, std::unique_ptr<CanfdSession> session, QObject* parent = nullptr);
    ~Ecat2CanfdController() override;

    QString state() const { return state_; }
    QString statusText() const;
    bool active() const { return ownsSession_; }
    bool busy() const { return busy_; }
    bool connected() const { return connected_; }
    bool running() const { return state_ == QStringLiteral("running"); }
    bool canStart() const;
    QString errorText() const { return errorText_; }
    int slaveCount() const { return int(slaves_.size()); }
    int selectedSlave() const { return selectedSlave_; }
    QVariantList slaves() const { return slaves_; }
    QVariantList channelConfigs() const;
    QVariantMap diagnostics() const { return diagnostics_; }
    QAbstractItemModel* frames() { return &frames_; }
    QVariantMap pdoSnapshot() const { return pdoSnapshot_; }
    bool periodicSending() const { return periodicSending_; }
    int periodicIntervalUs() const { return periodicIntervalUs_; }
    qulonglong receivedCount() const { return receivedCount_; }
    qulonglong droppedCount() const { return droppedCount_; }
    void setNicName(const std::string& nic);

    Q_INVOKABLE void connectBus(int expectedCount = 1);
    Q_INVOKABLE void disconnectBus();
    Q_INVOKABLE void start(int cycleUs = 2000, bool dcEnabled = false, int shiftUs = 0);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void readConfiguration(int slave = 0);
    Q_INVOKABLE void applyConfiguration(int slave, const QVariantList& configs);
    Q_INVOKABLE void selectSlave(int slave);
    Q_INVOKABLE void sendOnce(int slave, int channel, const QString& id, int dlc, const QString& data);
    Q_INVOKABLE void startPeriodic(int slave, int channel, const QString& id, int dlc, const QString& data, int intervalUs = 100000);
    Q_INVOKABLE void stopPeriodic();
    Q_INVOKABLE void clearFrames();
signals:
    void stateChanged();
    void resultsChanged();
    void logAppended(const QString& line);
    void errorOccurred(const QString& message);
    void sessionConnected(int slaveCount);
    void sessionReleased();
private:
    struct RuntimeData;
    void setState(const QString& state, bool busy);
    void fail(const QString& error);
    void refresh();
    void updatePdoSnapshot();
    void mergeConfigs(const std::vector<canfd::ChannelConfig>& configs);
    bool makeFrame(int slave, int channel, const QString& id, int dlc, const QString& data, canfd::Frame& frame);
    void clearOutgoing(int slave = 0, int channel = -1);
    void finishDisconnect(quint64 generation, const QString& error);
    static void prepare(const std::shared_ptr<RuntimeData>& runtime, std::vector<soem_interface::SlaveProcessImage>& images);
    static void consume(const std::shared_ptr<RuntimeData>& runtime, const soem_interface::ApplicationCycle& cycle);

    BusSessionCoordinator& coordinator_;
    std::unique_ptr<CanfdSession> session_;
    std::shared_ptr<RuntimeData> runtime_;
    std::string nicName_;
    QString state_{QStringLiteral("idle")};
    QString errorText_;
    bool ownsSession_{false}, connected_{false}, busy_{false}, protocolValid_{false}, periodicSending_{false}, dcEnabled_{false};
    quint64 generation_{0};
    std::atomic<bool> cancel_{false};
    int selectedSlave_{0}, cycleUs_{2000}, periodicIntervalUs_{100000};
    qulonglong receivedCount_{0}, droppedCount_{0}, historyDropped_{0};
    QVariantList slaves_;
    std::vector<canfd::ChannelConfig> configs_;
    QVariantMap diagnostics_, pdoSnapshot_;
    CanFrameModel frames_;
    QThreadPool workers_;
    QTimer refreshTimer_;
};
} // namespace Backend
