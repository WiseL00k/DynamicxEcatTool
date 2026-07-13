#pragma once

#include <QObject>
#include <QMutex>
#include <QString>

namespace Backend {

class BusSessionCoordinator : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Idle,
        Test,
        Communication,
        LegacyPreOp,
        MitDebug,
        Explorer,
        Flashing
    };
    Q_ENUM(Mode)

    explicit BusSessionCoordinator(QObject* parent = nullptr);

    Mode mode() const;
    bool active() const;
    QString modeName() const;
    bool tryAcquire(Mode requestedMode, QString& errorMessage);
    bool release(Mode expectedMode);
    void forceIdle();

signals:
    void sessionChanged();

private:
    static QString displayName(Mode mode);

    mutable QMutex mutex_;
    Mode mode_{Mode::Idle};
};

} // namespace Backend
