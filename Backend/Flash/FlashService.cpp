#include "FlashService.h"

#include "Backend/Ethercat/EthercatErrorMapper.h"
#include "SOEM_interface/SoemUtils.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <algorithm>
#include <cstdlib>
#include <memory>

namespace Backend {
namespace {

QString toLocalPath(const QString& filePath)
{
    const QUrl url(filePath);
    return url.isLocalFile() ? url.toLocalFile() : filePath;
}

void emitProgress(QPointer<FlashService> self, const QString& type, int percent)
{
    if (self) {
        emit self->flashProgress(type, percent);
    }
}

} // namespace

FlashService::FlashService(QObject* parent)
    : QObject(parent)
{}

bool FlashService::beginFlash(const QString& type)
{
    bool expected = false;
    if (!flashBusy_.compare_exchange_strong(expected, true)) {
        emit errorOccurred(QStringLiteral("已有烧录任务正在执行"));
        emit flashFinished(type, false, QStringLiteral("BUSY"));
        return false;
    }
    return true;
}

void FlashService::endFlash()
{
    flashBusy_.store(false);
}

bool FlashService::flashEEprom(const std::string& nicName, int slaveId, const QString& filePath)
{
    if (nicName.empty()) {
        emit errorOccurred(QStringLiteral("请先选择网卡"));
        return false;
    }

    if (slaveId <= 0) {
        emit errorOccurred(QStringLiteral("从站地址错误"));
        return false;
    }

    if (filePath.isEmpty()) {
        emit logUpdated(QStringLiteral("请选择HEX文件!"));
        emit errorOccurred(QStringLiteral("请选择Hex文件!"));
        return false;
    }

    if (!beginFlash(QStringLiteral("eeprom"))) {
        return false;
    }

    const std::string hexFilePath = toLocalPath(filePath).toStdString();
    auto progress = std::make_shared<std::atomic<int>>(0);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto timer = std::make_shared<QElapsedTimer>();
    const QPointer<FlashService> self(this);
    timer->start();

    QThreadPool::globalInstance()->start([self, progress, finished]() {
        while (!finished->load()) {
            int percent = progress->load();
            if (percent < 98) {
                const int delta = std::max(1, int((98 - percent) * (0.03 + (std::rand() % 5) * 0.01)));
                percent += delta;
                progress->store(percent);
                emitProgress(self, QStringLiteral("eeprom"), percent);
            }
            QThread::msleep(100);
        }
    });

    QThreadPool::globalInstance()->start([=]() {
        soem_interface::EEpromTool eepromTool(nicName, slaveId, MODE_WRITEINTEL, hexFilePath.c_str());

        if (self) {
            emit self->logUpdated(QStringLiteral("开始烧录 EEPROM..."));
        }

        const auto errorCode = eepromTool.work(nicName, slaveId, MODE_WRITEINTEL, hexFilePath.c_str());
        finished->store(true);

        if (!self) {
            return;
        }

        self->endFlash();
        if (errorCode != soem_interface::error::NoError) {
            const QString message = toUserMessage(errorCode);
            emit self->errorOccurred(message);
            emit self->flashFinished(QStringLiteral("eeprom"), false, message);
            return;
        }

        const qint64 elapsedMs = timer->elapsed();
        progress->store(100);
        emit self->flashProgress(QStringLiteral("eeprom"), 100);
        emit self->logUpdated(QStringLiteral("烧录完成，用时 %1 ms").arg(elapsedMs));
        emit self->flashFinished(QStringLiteral("eeprom"), true, QStringLiteral("OK (%1 ms)").arg(elapsedMs));
    });

    return true;
}

bool FlashService::flashFirmware(const std::string& nicName, int slaveId, const QString& filePath)
{
    if (nicName.empty()) {
        emit errorOccurred(QStringLiteral("请先选择网卡"));
        return false;
    }

    if (slaveId <= 0) {
        emit errorOccurred(QStringLiteral("从站地址错误"));
        return false;
    }

    if (filePath.isEmpty()) {
        emit logUpdated(QStringLiteral("请选择Bin文件!"));
        emit errorOccurred(QStringLiteral("请选择Bin文件!"));
        return false;
    }

    if (!beginFlash(QStringLiteral("firmware"))) {
        return false;
    }

    const std::string binFilePath = toLocalPath(filePath).toStdString();
    auto progress = std::make_shared<std::atomic<int>>(0);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto timer = std::make_shared<QElapsedTimer>();
    const QPointer<FlashService> self(this);
    timer->start();

    QThreadPool::globalInstance()->start([self, progress, finished]() {
        while (!finished->load()) {
            int percent = progress->load();
            if (percent < 99) {
                const int delta = std::max(1, int((99 - percent) * (0.03 + (std::rand() % 5) * 0.01)));
                percent += delta;
                progress->store(percent);
                emitProgress(self, QStringLiteral("firmware"), percent);
            }
            QThread::msleep(50);
        }
    });

    QThreadPool::globalInstance()->start([=]() {
        bool ok = false;
        soem_interface::FirmwareTool tool(nicName);

        try {
            if (self) {
                emit self->logUpdated(QStringLiteral("开始固件烧录..."));
            }

            if (tool.init()) {
                ok = tool.flashFirmware(static_cast<uint16_t>(slaveId), binFilePath);
            }

            if (self) {
                if (ok) {
                    emit self->logUpdated(QStringLiteral("固件烧录成功"));
                } else {
                    emit self->errorOccurred(QStringLiteral("固件烧录失败, 请检查固件文件名或网卡名是否正确!或重新烧录EEProm文件!"));
                }
            }

            tool.close();
        } catch (...) {
            if (self) {
                emit self->errorOccurred(QStringLiteral("固件烧录异常"));
            }
        }

        progress->store(100);
        finished->store(true);

        if (!self) {
            return;
        }

        self->endFlash();
        emit self->flashProgress(QStringLiteral("firmware"), 100);
        const qint64 elapsedMs = timer->elapsed();
        emit self->logUpdated(QStringLiteral("烧录结束，耗时 %1 ms").arg(elapsedMs));
        emit self->flashFinished(
            QStringLiteral("firmware"),
            ok,
            ok ? QStringLiteral("OK (%1 ms)").arg(elapsedMs) : QStringLiteral("FAIL (%1 ms)").arg(elapsedMs));
    });

    return true;
}

} // namespace Backend
