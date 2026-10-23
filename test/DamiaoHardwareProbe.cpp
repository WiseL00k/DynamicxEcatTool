#include "Backend/Ethercat/EthercatBackend.h"
#include "Backend/Commands/DamiaoMitController.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <functional>
#include <stdexcept>

namespace {
bool awaitCondition(const std::function<bool()>& condition, int timeoutMs = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return condition();
}

void require(bool condition, const QString& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

bool sameConfiguration(const QVariantList& expected, const QVariantList& actual)
{
    if (expected.size() != actual.size()) return false;
    for (qsizetype i = 0; i < expected.size(); ++i) {
        const auto before = expected[i].toMap(), after = actual[i].toMap();
        for (const auto* key : {"slave", "channel", "enabled", "format", "nominalBitrate", "dataBitrate", "options"})
            if (before.value(key) != after.value(key)) return false;
        if (!after.value("verified").toBool() || after.value("status").toInt() != 0 || after.value("error").toUInt() != 0)
            return false;
    }
    return true;
}
}

// Manual opt-in integration probe, deliberately excluded from CTest. It never
// submits a motor/CAN command; firmware transition behavior still applies.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Damiao panel controller hardware probe (no application CAN commands)");
    parser.addHelpOption();
    parser.addOption({"adapter", "Unique network adapter description substring", "description", "2.5GbE"});
    parser.addOption({"exercise-config", "Reapply and verify the original CAN settings in PRE-OP and OP"});
    parser.addOption({"verify-legacy-rejection", "Check that this ECAT2CANFD device is rejected in legacy MIT mode"});
    parser.addOption({"output", "JSON evidence path", "path"});
    parser.process(app);

    Backend::EthercatBackend backend;
    auto* controller = backend.damiaoMit();
    auto* canfd = controller->canfd();
    QJsonObject report{{"applicationCanCommands", 0}, {"success", false}, {"steadyCommunicationClean", true}};
    QJsonArray runs, logs, errors;
    QObject::connect(controller, &Backend::DamiaoMitController::logAppended, &app,
                     [&](const QString& message) { logs.append(message); });
    try {
        bool adaptersReady = false;
        QObject::connect(&backend, &Backend::EthercatBackend::nicListChanged, &app, [&] { adaptersReady = true; });
        backend.refreshNicsAsync();
        require(awaitCondition([&] { return adaptersReady; }), "Adapter enumeration timed out");
        const auto adapters = backend.nicList();
        QList<int> matches;
        for (int i = 0; i < adapters.size(); ++i)
            if (adapters[i].contains(parser.value("adapter"), Qt::CaseInsensitive)) matches.append(i);
        require(matches.size() == 1, "Expected exactly one matching network adapter");
        report["adapter"] = adapters[matches[0]];
        backend.changedSelectedNic(matches[0]);
        require(controller->setSlaveType("ecat2canfd"), "Could not select ECAT2CANFD");
        controller->connectBus();
        const bool connectionSettled = awaitCondition([&] { return !controller->busy(); });
        require(connectionSettled && controller->connected(),
                "ECAT2CANFD connection failed: " + controller->errorText());
        require(backend.slaveCount() == 1 && controller->canStart() && !controller->running(),
                "Expected one verified slave in PRE-OP");
        report["singleSlavePreOp"] = true;
        report["sessionMode"] = backend.sessionMode();
        const auto original = canfd->channelConfigs();
        report["originalConfigs"] = QJsonArray::fromVariantList(original);
        require(original.size() == 2 && sameConfiguration(original, original), "CAN configuration not verified");

        auto applyOriginal = [&] {
            canfd->applyConfiguration(1, original);
            require(awaitCondition([&] { return !controller->busy(); }), "SDO configuration timed out");
            require(sameConfiguration(original, canfd->channelConfigs()) && controller->errorText().isEmpty(),
                    "SDO configuration failed: " + controller->errorText());
        };
        if (parser.isSet("exercise-config")) {
            applyOriginal();
            report["preOpConfigReadbackMatches"] = true;
        }

        for (bool dc : {false, true}) {
            QTextStream(stdout) << "Checking Damiao controller " << (dc ? "DC OP" : "SM2 OP") << '\n';
            controller->start(2000, dc, 0);
            const bool startSettled = awaitCondition([&] { return !controller->busy(); });
            require(startSettled && controller->running(),
                    "OP start failed: " + controller->errorText());
            require(awaitCondition([&] { return canfd->diagnostics().value("cycleCount").toULongLong() >= 100; }),
                    "PDO cycles did not advance");
            const auto initialDiagnostics = canfd->diagnostics();
            const auto before = initialDiagnostics.value("cycleCount").toULongLong();
            const auto initialWkcErrors = initialDiagnostics.value("wkcErrorCount").toULongLong();
            canfd->readConfiguration(1);
            require(awaitCondition([&] { return !controller->busy(); }) && sameConfiguration(original, canfd->channelConfigs()),
                    "OP SDO readback failed");
            if (parser.isSet("exercise-config")) applyOriginal();
            require(awaitCondition([&] { return canfd->diagnostics().value("cycleCount").toULongLong() >= before + 2500; }),
                    "PDO cycles stopped during SDO operations");
            const auto diagnostics = canfd->diagnostics();
            require(diagnostics.value("wkc").toInt() == diagnostics.value("expectedWkc").toInt()
                    && diagnostics.value("expectedWkc").toInt() > 0, "Invalid working counter");
            require(diagnostics.value("submittedCount").toULongLong() == 0, "Unexpected application CAN submission");
            // Runtime totals include PRE-OP/SAFE-OP transitions. Measure the
            // stable OP interval separately so startup WKC is not misreported.
            const auto steadyWkcErrors = diagnostics.value("wkcErrorCount").toULongLong() - initialWkcErrors;
            if (steadyWkcErrors != 0) report["steadyCommunicationClean"] = false;
            QJsonObject run{{"dc", dc}, {"opReadbackMatches", true},
                            {"configExercised", parser.isSet("exercise-config")},
                            {"initialDiagnostics", QJsonObject::fromVariantMap(initialDiagnostics)},
                            {"steadyWkcErrors", double(steadyWkcErrors)},
                            {"diagnostics", QJsonObject::fromVariantMap(diagnostics)}};
            controller->stop();
            const bool stopSettled = awaitCondition([&] { return !controller->busy(); });
            require(stopSettled && controller->connected() && controller->canStart(),
                    "Failed to return to PRE-OP: " + controller->errorText());
            run["returnedToPreOp"] = true;
            runs.append(run);
        }
        controller->disconnectBus();
        require(awaitCondition([&] { return !controller->active(); }) && !backend.sessionActive(), "Session not released");
        report["disconnected"] = true;
        if (parser.isSet("verify-legacy-rejection")) {
            require(controller->setSlaveType("mit"), "Could not select legacy MIT");
            controller->connectBus();
            require(awaitCondition([&] { return !controller->busy(); }) && !controller->connected()
                    && !controller->active() && !backend.sessionActive() && !controller->errorText().isEmpty(),
                    "ECAT2CANFD must be rejected by legacy MIT mode");
            report["legacyTypeRejected"] = true;
            report["legacyRejectionReason"] = controller->errorText();
        }
        report["success"] = true;
    } catch (const std::exception& exception) {
        errors.append(QString::fromUtf8(exception.what()));
        QTextStream(stderr) << exception.what() << '\n';
    }
    if (controller->active()) {
        controller->disconnectBus();
        if (!awaitCondition([&] { return !controller->active(); })) {
            report["success"] = false;
            errors.append("Cleanup did not release the session");
        }
    }
    report["runs"] = runs;
    report["logs"] = logs;
    report["errors"] = errors;
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (parser.isSet("output")) {
        QFile output(parser.value("output"));
        if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size()) return 2;
    }
    QTextStream(stdout) << json;
    return report.value("success").toBool() ? 0 : 1;
}
