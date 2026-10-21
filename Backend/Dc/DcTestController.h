#pragma once

#include "SOEM_interface/DcTestTypes.h"

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <string>

namespace Backend {

class BusSessionCoordinator;
class EthercatMasterController;

class DcTestSession
{
public:
    virtual ~DcTestSession() = default;

    virtual soem_interface::DcStartResult start(
        const std::string& nicName,
        const soem_interface::DcTestOptions& options) = 0;
    virtual void requestStop() = 0;
    virtual void stop() = 0;
    virtual soem_interface::DcTestSnapshot snapshot() const = 0;
    virtual void resetStatistics() = 0;
};

class DcTestController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged)
    Q_PROPERTY(bool historical READ historical NOTIFY historicalChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList slavesModel READ slavesModel NOTIFY resultsChanged)

public:
    DcTestController(BusSessionCoordinator& coordinator,
                     EthercatMasterController& masterController,
                     QObject* parent = nullptr);
    DcTestController(BusSessionCoordinator& coordinator,
                     std::unique_ptr<DcTestSession> session,
                     QObject* parent = nullptr);
    ~DcTestController() override;

    QString state() const;
    QString statusText() const;
    bool active() const;
    bool busy() const;
    bool hasResults() const;
    bool historical() const;
    QVariantMap diagnostics() const;
    QVariantList slavesModel() const;

    void setNicName(const std::string& nicName);

    Q_INVOKABLE void start(qint64 cycleUs, qint64 shiftUs);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void resetStatistics();

signals:
    void stateChanged();
    void resultsChanged();
    void historicalChanged();
    void logAppended(const QString& line);
    void errorOccurred(const QString& message);

    // Used by EthercatBackend to keep its legacy global connection view in sync.
    void sessionConnected(int slaveCount);
    void sessionReleased();

private:
    enum class State {
        Idle,
        Starting,
        Running,
        Stopping,
        Failed
    };

    void setState(State state);
    void appendLog(const QString& line);
    void refreshSnapshot();
    void publishSnapshot(const soem_interface::DcTestSnapshot& snapshot);
    void completeStart(quint64 generation,
                       soem_interface::DcStartResult result,
                       soem_interface::DcTestSnapshot snapshot);
    void scheduleCleanup(quint64 generation, State finalState, QString errorMessage = {});
    void completeCleanup(quint64 generation,
                         State finalState,
                         QString errorMessage,
                         soem_interface::DcTestSnapshot snapshot);

    static bool isActiveState(State state);
    static bool isBusyState(State state);
    static QVariantMap diagnosticsMap(const soem_interface::DcTestSnapshot& snapshot);
    static QVariantList slaveMaps(const soem_interface::DcTestSnapshot& snapshot);

    BusSessionCoordinator& coordinator_;
    std::unique_ptr<DcTestSession> session_;
    std::string nicName_;
    State state_{State::Idle};
    bool hasResults_{false};
    bool ownsSession_{false};
    bool cleanupScheduled_{false};
    bool destroying_{false};
    quint64 generation_{0};
    QVariantMap diagnostics_;
    QVariantList slaves_;
    QTimer refreshTimer_;
    QThreadPool workerPool_;
};

} // namespace Backend
