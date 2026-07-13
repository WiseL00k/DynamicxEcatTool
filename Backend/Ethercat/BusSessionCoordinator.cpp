#include "BusSessionCoordinator.h"

#include <QMutexLocker>

namespace Backend {

BusSessionCoordinator::BusSessionCoordinator(QObject* parent)
    : QObject(parent)
{}

BusSessionCoordinator::Mode BusSessionCoordinator::mode() const
{
    QMutexLocker locker(&mutex_);
    return mode_;
}

bool BusSessionCoordinator::active() const
{
    return mode() != Mode::Idle;
}

QString BusSessionCoordinator::modeName() const
{
    return displayName(mode());
}

bool BusSessionCoordinator::tryAcquire(Mode requestedMode, QString& errorMessage)
{
    if (requestedMode == Mode::Idle) {
        errorMessage = QStringLiteral("无效的总线会话类型");
        return false;
    }

    {
        QMutexLocker locker(&mutex_);
        if (mode_ != Mode::Idle) {
            errorMessage = QStringLiteral("当前总线正用于%1，请先结束该会话").arg(displayName(mode_));
            return false;
        }
        mode_ = requestedMode;
    }

    emit sessionChanged();
    return true;
}

bool BusSessionCoordinator::release(Mode expectedMode)
{
    {
        QMutexLocker locker(&mutex_);
        if (mode_ != expectedMode) {
            return false;
        }
        mode_ = Mode::Idle;
    }

    emit sessionChanged();
    return true;
}

void BusSessionCoordinator::forceIdle()
{
    {
        QMutexLocker locker(&mutex_);
        if (mode_ == Mode::Idle) {
            return;
        }
        mode_ = Mode::Idle;
    }

    emit sessionChanged();
}

QString BusSessionCoordinator::displayName(Mode mode)
{
    switch (mode) {
    case Mode::Idle:
        return QStringLiteral("空闲");
    case Mode::Test:
        return QStringLiteral("测试");
    case Mode::Communication:
        return QStringLiteral("调试通信");
    case Mode::LegacyPreOp:
        return QStringLiteral("Pre-OP");
    case Mode::MitDebug:
        return QStringLiteral("MIT参数调试");
    case Mode::Explorer:
        return QStringLiteral("总线配置");
    case Mode::Flashing:
        return QStringLiteral("固件或EEPROM烧录");
    }
    return QStringLiteral("未知会话");
}

} // namespace Backend
