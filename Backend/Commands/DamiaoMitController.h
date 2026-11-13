#pragma once

#include "Backend/Canfd/Ecat2CanfdController.h"
#include "Backend/Ethercat/BusSessionCoordinator.h"
#include <QObject>
#include <QVariantMap>

namespace Backend {

// The legacy path delegates to the existing backend; the CAN gateway path owns
// its controller and shares only the exclusive bus-session coordinator.
class DamiaoMitController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString slaveType READ slaveType NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(bool periodicSending READ periodicSending NOTIFY stateChanged)
    Q_PROPERTY(int periodicIntervalUs READ periodicIntervalUs NOTIFY stateChanged)
    Q_PROPERTY(bool targetLocked READ targetLocked NOTIFY stateChanged)
    Q_PROPERTY(int targetChannel READ targetChannel NOTIFY stateChanged)
    Q_PROPERTY(QString targetIdHex READ targetIdHex NOTIFY stateChanged)
    Q_PROPERTY(Ecat2CanfdController* canfd READ canfd CONSTANT)
public:
    explicit DamiaoMitController(BusSessionCoordinator&, QObject* parent = nullptr);
    DamiaoMitController(BusSessionCoordinator&, std::unique_ptr<CanfdSession>, QObject* parent = nullptr);
    QString slaveType() const { return slaveType_; }
    QString state() const;
    bool active() const;
    bool connected() const;
    bool busy() const;
    bool running() const;
    bool canStart() const;
    QString statusText() const;
    QString errorText() const { return errorText_; }
    bool periodicSending() const;
    int periodicIntervalUs() const;
    bool targetLocked() const;
    int targetChannel() const { return targetChannel_; }
    QString targetIdHex() const;
    Ecat2CanfdController* canfd() { return &canfd_; }
    void setNicName(const std::string& nic);
    void setLegacyConnected(bool connected);
    void reportLegacyError(const QString& error);

    Q_INVOKABLE bool setSlaveType(const QString& type);
    Q_INVOKABLE void connectBus();
    Q_INVOKABLE void disconnectBus();
    Q_INVOKABLE void start(int cycleUs = 2000, bool dcEnabled = false, int shiftUs = 0);
    Q_INVOKABLE void stop();
    Q_INVOKABLE QVariantMap preview(const QVariantMap& values) const;
    Q_INVOKABLE bool sendControl(int channel, const QString& id, const QVariantMap& values,
                                bool periodic = false, int intervalUs = 2000);
    Q_INVOKABLE bool sendRaw(int channel, const QString& id, const QString& data,
                            bool periodic = false, int intervalUs = 2000);
    Q_INVOKABLE bool enableMotor(int channel, const QString& id);
    Q_INVOKABLE bool disableMotor(int channel, const QString& id);
    Q_INVOKABLE bool stopSending(int channel, const QString& id);
signals:
    void stateChanged();
    void logAppended(const QString& message);
    void errorOccurred(const QString& message);
    void legacyConnectRequested();
    void legacyDisconnectRequested();
    void legacySendRequested(int channel, int id, const QVariantList& bytes);
    void legacyClearRequested(int channel, int id);
    void legacyEnableRequested();
    void legacyDisableRequested();
private:
    bool gateway() const { return slaveType_ == QStringLiteral("ecat2canfd"); }
    bool fail(const QString& message);
    bool parseTarget(int channel, const QString& text, int& id);
    bool ready(int channel);
    bool special(int channel, const QString& id, quint8 opcode);
    BusSessionCoordinator& coordinator_;
    Ecat2CanfdController canfd_;
    QString slaveType_{QStringLiteral("mit")}, errorText_;
    bool legacyConnected_{false}, legacyBusy_{false};
    bool errorFromCanfd_{false};
    int targetChannel_{0}, targetId_{1};
};
} // namespace Backend
