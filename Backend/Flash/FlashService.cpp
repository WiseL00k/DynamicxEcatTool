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
#include <exception>
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
        emit self->flashFinished(QStringLiteral("eeprom"), true, QStringLiteral("成功（%1 ms）").arg(elapsedMs));
    });

    return true;
}

bool FlashService::flashFirmware(const std::string& nicName, int slaveId, const QString& filePath)
{
    if (nicName.empty()) {
        emit errorOccurred(QStringLiteral("请先选择网卡"));
        return false;
    }

    if (slaveId <= 0 || slaveId > 65535) {
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

    const QString localPath = toLocalPath(filePath);
#ifdef _WIN32
    const std::filesystem::path binFilePath(localPath.toStdWString());
#else
    const std::filesystem::path binFilePath(localPath.toStdString());
#endif
    auto timer = std::make_shared<QElapsedTimer>();
    const QPointer<FlashService> self(this);
    timer->start();

    // No byte-level progress is available here. Keep the UI indeterminate until
    // the final FoE ACK instead of fabricating progress or emitting 100% on error.
    emit flashProgress(QStringLiteral("firmware"), 0);
    QThreadPool::globalInstance()->start([=]() {
        bool ok = false;
        QString message;
        try {
            auto tool = std::make_unique<soem_interface::FirmwareTool>(nicName);
            if (self) {
                emit self->logUpdated(QStringLiteral("开始固件烧录..."));
            }
            // File validation must happen before init() writes to the bus.
            ok = tool->flashFirmware(static_cast<uint16_t>(slaveId), binFilePath);
            message = QString::fromStdString(tool->lastResult().message);
        } catch (const std::exception& error) {
            message = QStringLiteral("固件烧录异常 [FW_EXCEPTION]\n从站：%1\n\n失败原因\n%2")
                .arg(slaveId).arg(QString::fromUtf8(error.what()));
        } catch (...) {
            message = QStringLiteral("固件烧录异常 [FW_EXCEPTION]\n从站：%1\n\n失败原因\n发生未知异常")
                .arg(slaveId);
        }
        if (!self) {
            return;
        }
        self->endFlash();
        const qint64 elapsedMs = timer->elapsed();
        message += QStringLiteral("\n\n耗时：%1 ms").arg(elapsedMs);
        if (ok) emit self->flashProgress(QStringLiteral("firmware"), 100);
        else emit self->errorOccurred(message);
        emit self->logUpdated(message);
        emit self->flashFinished(
            QStringLiteral("firmware"), ok, message);
    });

    return true;
}

} // namespace Backend
