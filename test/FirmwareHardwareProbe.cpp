#include "SOEM_interface/SoemUtils.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <exception>
#include <memory>

// Manual probe of the production FirmwareTool, never registered with CTest.
// With no --flash it only prints help; no NIC is opened or initialized.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("SOEM FoE firmware probe. Stop other masters on the adapter first.");
    parser.addHelpOption();
    parser.addOption({"flash", "Download firmware and request BOOT exit on success (device resets)"});
    parser.addOption({"adapter", "Exact adapter name, e.g. eno1", "name"});
    parser.addOption({"slave", "SOEM slave position (starts at 1)", "position", "1"});
    parser.addOption({"firmware", "Path to the complete firmware package", "path"});
    parser.addOption({"output", "Save structured JSON evidence", "path"});
    parser.process(app);
    if (!parser.isSet("flash")) parser.showHelp(0);

    bool validSlave = false;
    const uint slave = parser.value("slave").toUInt(&validSlave);
    if (!validSlave || !slave || slave > 65535 || parser.value("adapter").isEmpty()
        || parser.value("firmware").isEmpty()) {
        QTextStream(stderr) << "--flash requires --adapter, --firmware and a slave position from 1 to 65535\n";
        return 2;
    }

    QJsonObject report{{"adapter", parser.value("adapter")}, {"slave", int(slave)},
                       {"firmware", parser.value("firmware")}, {"success", false}};
    // Evidence output must be writable before any bus state change.
    QFile output(parser.value("output"));
    if (parser.isSet("output") && !output.open(QIODevice::WriteOnly)) {
        QTextStream(stderr) << "Cannot open evidence file: " << output.errorString() << '\n';
        return 2;
    }
    QFile firmware(parser.value("firmware"));
    if (firmware.open(QIODevice::ReadOnly)) {
        QCryptographicHash digest(QCryptographicHash::Sha256);
        digest.addData(&firmware);
        report["sha256"] = QString::fromLatin1(digest.result().toHex());
        report["bytes"] = firmware.size();
    }

    QElapsedTimer elapsed;
    elapsed.start();
    try {
#ifdef _WIN32
        const std::filesystem::path path(parser.value("firmware").toStdWString());
#else
        const std::filesystem::path path(parser.value("firmware").toStdString());
#endif
        auto tool = std::make_unique<soem_interface::FirmwareTool>(parser.value("adapter").toStdString());
        report["success"] = tool->flashFirmware(uint16_t(slave), path);
        const auto& result = tool->lastResult();
        report["stageCode"] = QStringLiteral("FW%1").arg(int(result.error), 3, 10, QLatin1Char('0'));
        report["soemReturn"] = result.soemReturn;
        report["foeError"] = QStringLiteral("0x%1").arg(result.foeError, 8, 16, QLatin1Char('0'));
        report["mailboxError"] = int(result.mailboxError);
        report["alStateValid"] = result.alStateValid;
        if (result.alStateValid) {
            report["alState"] = int(result.alState);
            report["alStatusCode"] = int(result.alStatusCode);
        }
        report["downloadComplete"] = result.downloadComplete;
        report["activationRequested"] = result.activationRequested;
        report["message"] = QString::fromStdString(result.message);
        report["installationVerified"] = false; // Separate post-reset readback required.
        QTextStream(stderr) << QString::fromStdString(result.message) << '\n';
    } catch (const std::exception& error) {
        report["exception"] = QString::fromUtf8(error.what());
    } catch (...) {
        report["exception"] = "Unknown exception";
    }
    report["elapsedMs"] = elapsed.elapsed();
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    QTextStream(stdout) << json;
    if (output.isOpen() && (output.write(json) != json.size() || !output.flush())) return 2;
    return report.value("success").toBool() ? 0 : 1;
}
