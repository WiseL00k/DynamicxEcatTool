#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <string>

namespace Backend {

class FlashService : public QObject
{
    Q_OBJECT

public:
    explicit FlashService(QObject* parent = nullptr);

    void flashEEprom(const std::string& nicName, int slaveId, const QString& filePath);
    void flashFirmware(const std::string& nicName, int slaveId, const QString& filePath);

signals:
    void logUpdated(const QString& line);
    void errorOccurred(const QString& message);
    void flashProgress(QString type, int percent);
    void flashFinished(QString type, bool success, QString msg);

private:
    bool beginFlash(const QString& type);
    void endFlash();

    std::atomic_bool flashBusy_{false};
};

} // namespace Backend
