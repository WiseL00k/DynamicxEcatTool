#include <QtTest>
#include <QFile>
#include "SOEM_interface/SoemUtils.h"

using namespace soem_interface;

class FirmwareMessageTests : public QObject {
    Q_OBJECT
private slots:
    void reportedErrorKeepsCodesStateAndRecoveryInSeparateGroups()
    {
        FirmwareFlashResult result;
        result.error = FirmwareFlashError::TransferFailed;
        result.slave = 1;
        result.soemReturn = -5;
        result.foeError = 0x8000;
        result.alState = EC_STATE_BOOT;
        result.alStateValid = true;
        result.reason = "从站返回 FoE ERROR";
        const auto text = QString::fromStdString(formatFirmwareFlashResult(result));
        QVERIFY(text.startsWith("固件下载失败 [FW010]\n从站：1"));
        QVERIFY(text.contains("\n\n失败原因\n从站返回 FoE ERROR"));
        QVERIFY(text.contains("\n\n错误码\nSOEM 返回值：-5\nFoE 错误码：0x00008000"));
        QVERIFY(text.contains("AL 状态：BOOT（0x0003）\nAL 错误码：0x0000（无错误）"));
        QVERIFY(text.contains("\n\n处理说明\n未发送退出 BOOT 请求"));
        QVERIFY(!text.contains("固件下载已确认"));
        const auto output = qEnvironmentVariable("FIRMWARE_MESSAGE_SAMPLE");
        if (!output.isEmpty()) {
            QFile file(output);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text.toUtf8());
        }
    }
    void localValidationDoesNotInventTransportOrSlaveDiagnostics()
    {
        FirmwareFlashResult result;
        result.error = FirmwareFlashError::InvalidPackage;
        result.slave = 1;
        result.reason = "缺少 EC2PKG01 包头";
        const auto text = QString::fromStdString(formatFirmwareFlashResult(result));
        QVERIFY(text.contains("FW012"));
        QVERIFY(text.contains("固件文件检查"));
        QVERIFY(text.contains("缺少 EC2PKG01 包头"));
        QVERIFY(!text.contains("SOEM"));
        QVERIFY(!text.contains("AL 状态"));
    }
    void unknownStateIsDistinctFromAnErrorFlag()
    {
        FirmwareFlashResult result;
        result.error = FirmwareFlashError::BootFailed;
        result.alStateValid = true;
        result.alState = EC_STATE_BOOT | EC_STATE_ERROR;
        result.alStatusCode = 0x0015;
        const auto error = QString::fromStdString(formatFirmwareFlashResult(result));
        QVERIFY(error.contains("BOOT + ERROR（0x0013）"));
        QVERIFY(error.contains("0x0015"));
        result.alStateValid = false;
        const auto missing = QString::fromStdString(formatFirmwareFlashResult(result));
        QVERIFY(missing.contains("AL 状态：未能回读"));
        QVERIFY(!missing.contains("AL 错误码：0x0000"));
    }
    void activationFailureRetainsTheCompletedDownload()
    {
        FirmwareFlashResult result;
        result.error = FirmwareFlashError::ActivationFailed;
        result.downloadComplete = true;
        const auto text = QString::fromStdString(formatFirmwareFlashResult(result));
        QVERIFY(text.startsWith("固件已下载，激活请求失败 [FW011]"));
        QVERIFY(text.contains("安装与启动结果尚未确认"));
    }
    void emptyResultDoesNotClaimDownloadSuccess()
    {
        const auto text = QString::fromStdString(formatFirmwareFlashResult({}));
        QVERIFY(text.startsWith("尚未执行固件下载"));
        QVERIFY(!text.contains("已确认"));
    }
};
QTEST_GUILESS_MAIN(FirmwareMessageTests)
#include "FirmwareMessageTests.moc"
