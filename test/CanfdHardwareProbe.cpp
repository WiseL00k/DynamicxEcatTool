#include "Backend/Canfd/CanfdSession.h"
#include "SOEM_interface/SoemUtils.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
double elapsedMs(Clock::time_point since) {
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}
QJsonArray configsJson(const Backend::CanfdConfigResult& result) {
    QJsonArray array;
    for (const auto& config : result.configs) array.append(QJsonObject::fromVariantMap(config.toMap()));
    return array;
}
bool sameSettings(const Backend::CanfdConfigResult& expected, const Backend::CanfdConfigResult& actual) {
    if (!expected.success || !actual.success || expected.configs.size() != actual.configs.size()) return false;
    for (const auto& wanted : expected.configs) {
        const auto found = std::find_if(actual.configs.begin(), actual.configs.end(), [&](const auto& item) {
            return item.slave == wanted.slave && item.channel == wanted.channel;
        });
        if (found == actual.configs.end() || found->enabled != wanted.enabled || found->format != wanted.format ||
            found->nominalBitrate != wanted.nominalBitrate || found->dataBitrate != wanted.dataBitrate ||
            found->options != wanted.options) return false;
    }
    return true;
}
QJsonObject snapshotJson(const soem_interface::DcTestSnapshot& snapshot) {
    QJsonArray slaves;
    for (const auto& slave : snapshot.slaves) {
        slaves.append(QJsonObject{{"position", slave.address}, {"state", slave.state},
            {"alStatusCode", slave.alStatusCode}, {"alStatusText", QString::fromStdString(slave.alStatusText)},
            {"hasDc", slave.hasDc}, {"sync0Requested", slave.sync0Requested},
            {"sync0Verified", slave.sync0Verified}, {"shutdownAttempted", slave.shutdownAttempted},
            {"shutdownVerified", slave.shutdownVerified}, {"cycleNs", double(slave.cycleNs)},
            {"shiftNs", slave.shiftNs}});
    }
    return {{"configured", snapshot.configured}, {"operational", snapshot.operational},
        {"finished", snapshot.finished}, {"referenceSlave", snapshot.referenceSlave},
        {"dcSlaveCount", snapshot.dcSlaveCount}, {"dcTimeValid", snapshot.dcTimeValid},
        {"dcTimeNs", QString::number(snapshot.dcTimeNs)}, {"phaseValid", snapshot.phaseValid},
        {"phaseErrorNs", double(snapshot.phaseErrorNs)}, {"correctionNs", double(snapshot.correctionNs)},
        {"workingCounter", snapshot.wkc}, {"expectedWorkingCounter", snapshot.expectedWkc},
        {"cycleCount", double(snapshot.cycleCount)}, {"slaves", slaves}};
}
bool allAt(const soem_interface::DcTestSnapshot& snapshot, uint16_t state) {
    return !snapshot.slaves.empty() && std::all_of(snapshot.slaves.begin(), snapshot.slaves.end(),
        [state](const auto& slave) { return slave.state == state && slave.alStatusCode == 0; });
}
bool sync0Verified(const soem_interface::DcTestSnapshot& snapshot, bool enabled) {
    return !snapshot.slaves.empty() && (!enabled || snapshot.dcSlaveCount > 0) &&
        std::all_of(snapshot.slaves.begin(), snapshot.slaves.end(), [enabled](const auto& slave) {
            return !slave.hasDc || (enabled ? slave.sync0Requested && slave.sync0Verified
                                           : slave.shutdownAttempted && slave.shutdownVerified);
        });
}
struct Counters {
    std::atomic<unsigned> captures{0}, valid{0}, errors{0};
    std::atomic<int64_t> maxGapNs{0}, maxBurstGapNs{0};
    std::atomic<bool> inBurst{false}, sawDcTime{false};
    int64_t lastHostTimeNs{0}; // Written only by the process-data callback.
};
}

// Explicit opt-in probe; never registered with CTest. It submits only zero PDOs.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("ECAT2CANFD PRE-OP / OP / DC SDO probe (zero application outputs)");
    parser.addHelpOption();
    parser.addOption({"adapter", "Exact Npcap adapter name", "name"});
    parser.addOption({"exercise-config", "Reapply the original CAN settings in PRE-OP and OP"});
    parser.addOption({"output", "Write JSON evidence", "path"});
    parser.process(app);
    if (!parser.isSet("adapter")) {
        for (const auto& nic : soem_interface::SoemUtils::scanAdapters())
            QTextStream(stdout) << QString::fromStdString(nic.name) << " | " << QString::fromStdString(nic.desc) << '\n';
        return 0;
    }
    auto session = Backend::makeCanfdSession();
    QJsonObject report{{"adapter", parser.value("adapter")}, {"exerciseConfig", parser.isSet("exercise-config")},
        {"applicationOutputs", "all zero; no CAN user frames submitted"}, {"targetSampleMsPerMode", 5000}};
    QJsonArray errors, runs;
    std::atomic<bool> cancelled{false};
    bool success = true;
    const auto fail = [&](const QString& message) {
        success = false;
        errors.append(message);
        QTextStream(stderr) << message << '\n';
    };
    try {
        const auto scan = session->connectBus(parser.value("adapter").toStdString());
        report["slaveCount"] = scan.slaveCount;
        report["expectedWkc"] = scan.expectedWorkingCounter;
        QString error;
        if (!Backend::canfd::validateScan(scan, 1, error)) fail(error);
        if (success) {
            report["identityVerified"] = Backend::verifyCanfdIdentity(*session, 1, error);
            if (!report["identityVerified"].toBool()) fail(error);
        }
        Backend::CanfdConfigResult original;
        if (success) {
            original = Backend::readCanfdConfiguration(*session, {1}, cancelled);
            report["preOpRead"] = original.success;
            report["originalConfigs"] = configsJson(original);
            if (!original.success) fail(original.error);
        }
        if (success && parser.isSet("exercise-config")) {
            const auto applied = Backend::applyCanfdConfiguration(*session, original.configs, cancelled);
            const bool matched = sameSettings(original, applied);
            report["preOpApply"] = applied.success;
            report["preOpApplyReadbackMatches"] = matched;
            report["preOpAppliedConfigs"] = configsJson(applied);
            if (!applied.success || !matched) fail(applied.error.isEmpty() ? "PRE-OP configuration readback mismatch" : applied.error);
        }
        if (success) for (bool dc : {false, true}) {
            QTextStream(stdout) << "Testing " << (dc ? "DC OP" : "SM2 OP") << '\n';
            auto counters = std::make_shared<Counters>();
            QJsonObject run{{"dc", dc}};
            bool runSuccess = true;
            const auto runFail = [&](const QString& message) {
                runSuccess = false;
                fail(QString(dc ? "DC: " : "SM2: ") + message);
            };
            try {
                const auto start = session->start({2000, dc, 0}, [](auto& images) {
                    for (auto& image : images) std::fill(image.outputs.begin(), image.outputs.end(), 0);
                }, [counters, dc](const auto& cycle) {
                    ++counters->captures;
                    if (cycle.workingCounter == cycle.expectedWorkingCounter) {
                        ++counters->valid;
                        if (dc && cycle.dcTimeNs > 0) counters->sawDcTime = true;
                    } else ++counters->errors;
                    if (counters->lastHostTimeNs) {
                        const auto gap = cycle.hostTimeNs - counters->lastHostTimeNs;
                        counters->maxGapNs = std::max(counters->maxGapNs.load(), gap);
                        if (counters->inBurst) counters->maxBurstGapNs = std::max(counters->maxBurstGapNs.load(), gap);
                    }
                    counters->lastHostTimeNs = cycle.hostTimeNs;
                });
                run["started"] = start.success;
                run["startError"] = QString::fromStdString(start.error);
                if (start.success) {
                    const auto sampleStart = Clock::now();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    const auto before = counters->captures.load();
                    auto operationStart = Clock::now();
                    const auto read = Backend::readCanfdConfiguration(*session, {1}, cancelled);
                    run["opReadMs"] = elapsedMs(operationStart);
                    run["opRead"] = read.success;
                    run["opReadMatchesOriginal"] = sameSettings(original, read);
                    if (!sameSettings(original, read)) runFail(read.error.isEmpty() ? "OP configuration readback mismatch" : read.error);
                    if (parser.isSet("exercise-config")) {
                        operationStart = Clock::now();
                        const auto applied = Backend::applyCanfdConfiguration(*session, original.configs, cancelled);
                        run["opApplyMs"] = elapsedMs(operationStart);
                        run["opApply"] = applied.success;
                        run["opApplyReadbackMatches"] = sameSettings(original, applied);
                        run["opAppliedConfigs"] = configsJson(applied);
                        if (!sameSettings(original, applied)) runFail(applied.error.isEmpty() ? "OP applied configuration readback mismatch" : applied.error);
                    }
                    run["pdoCyclesDuringSdo"] = int(counters->captures.load() - before);
                    QJsonObject burst{{"object", "0x1018:03"}, {"expectedSize", 4}, {"targetMs", 300}};
                    QJsonArray transferErrors;
                    unsigned transfers = 0, successful = 0, overlappingCalls = 0, transferValidCycles = 0;
                    int minWkc = 0, maxWkc = 0, minSize = 0, maxSize = 0;
                    double maxTransferMs = 0;
                    const auto burstCaptures = counters->captures.load(), burstValid = counters->valid.load();
                    counters->inBurst = true;
                    const auto burstStart = Clock::now();
                    do {
                        std::array<quint8, 4> bytes{};
                        const auto validBefore = counters->valid.load();
                        const auto transferStart = Clock::now();
                        const auto transfer = session->readSdo(1, 0x1018, 3, 4, bytes.data());
                        maxTransferMs = std::max(maxTransferMs, elapsedMs(transferStart));
                        const auto progress = counters->valid.load() - validBefore;
                        transferValidCycles += progress;
                        if (progress) ++overlappingCalls;
                        ++transfers;
                        minWkc = transfers == 1 ? transfer.wkc : std::min(minWkc, transfer.wkc);
                        maxWkc = transfers == 1 ? transfer.wkc : std::max(maxWkc, transfer.wkc);
                        minSize = transfers == 1 ? transfer.actualSize : std::min(minSize, transfer.actualSize);
                        maxSize = transfers == 1 ? transfer.actualSize : std::max(maxSize, transfer.actualSize);
                        if (transfer.success && transfer.actualSize == 4 && Backend::canfd::readLe(bytes.data(), 4) == 3) ++successful;
                        else {
                            transferErrors.append(QJsonObject{{"wkc", transfer.wkc}, {"actualSize", transfer.actualSize},
                                {"abortCode", Backend::canfd::hex(transfer.abortCode, 8)},
                                {"value", double(Backend::canfd::readLe(bytes.data(), 4))},
                                {"error", QString::fromStdString(transfer.error)}});
                            break;
                        }
                    } while (Clock::now() - burstStart < std::chrono::milliseconds(300));
                    counters->inBurst = false;
                    burst["elapsedMs"] = elapsedMs(burstStart);
                    burst["transfers"] = int(transfers); burst["successfulTransfers"] = int(successful);
                    burst["minWkc"] = minWkc; burst["maxWkc"] = maxWkc;
                    burst["minActualSize"] = minSize; burst["maxActualSize"] = maxSize;
                    burst["maxTransferMs"] = maxTransferMs;
                    burst["pdoCycles"] = int(counters->captures.load() - burstCaptures);
                    const auto validDuringBurst = counters->valid.load() - burstValid;
                    burst["validPdoCycles"] = int(validDuringBurst);
                    burst["callsWithValidPdoProgress"] = int(overlappingCalls);
                    burst["validPdoCyclesDuringTransfers"] = int(transferValidCycles);
                    burst["maxCallbackIntervalNs"] = double(counters->maxBurstGapNs.load());
                    burst["transferErrors"] = transferErrors;
                    burst["success"] = successful == transfers && transfers > 0 && validDuringBurst > 0;
                    run["sdoBurst"] = burst;
                    if (!burst["success"].toBool()) runFail("The SDO burst failed or no valid PDO cycles occurred during it");
                    std::this_thread::sleep_until(sampleStart + std::chrono::seconds(5));
                    run["sampleMs"] = elapsedMs(sampleStart);
                    const auto active = session->snapshot();
                    run["activeSnapshot"] = snapshotJson(active);
                    const bool activeVerified = active.operational && allAt(active, 8) && sync0Verified(active, dc) &&
                        (!dc || (active.dcTimeValid && active.phaseValid && counters->sawDcTime.load()));
                    run["activeStateAndDcVerified"] = activeVerified;
                    if (!activeVerified) runFail("Active OP / DC / SYNC0 readback verification failed");
                } else runFail("Could not start the application: " + QString::fromStdString(start.error));
            } catch (const std::exception& ex) { runFail("Run exception: " + QString::fromUtf8(ex.what())); }
            catch (...) { runFail("Unknown run exception"); }
            counters->inBurst = false;
            try {
                session->stop();
                const auto snapshot = session->snapshot();
                run["stoppedSnapshot"] = snapshotJson(snapshot);
                run["captures"] = int(counters->captures.load());
                run["validCycles"] = int(counters->valid.load());
                run["opWkcErrors"] = int(counters->errors.load());
                run["opWkcStable"] = counters->errors.load() == 0;
                run["maxOpCallbackIntervalNs"] = double(counters->maxGapNs.load());
                run["wkcErrors"] = double(snapshot.wkcErrorCount);
                run["wkcErrorsScope"] = "includes configuration, preheat, state transitions and cleanup; opWkcErrors counts OP callbacks only";
                run["missedCycles"] = double(snapshot.missedCycles);
                run["maxDeviationNs"] = double(snapshot.maxDeviationNs);
                run["runtimeError"] = QString::fromStdString(snapshot.error);
                run["cleanupWarning"] = QString::fromStdString(snapshot.cleanupWarning);
                const bool stoppedVerified = snapshot.finished && allAt(snapshot, 2) && sync0Verified(snapshot, false);
                run["preOpAndSync0ShutdownVerified"] = stoppedVerified;
                if (!snapshot.error.empty() || !snapshot.cleanupWarning.empty() || !counters->valid.load() || !stoppedVerified)
                    runFail("Runtime / PRE-OP cleanup verification failed");
                const auto after = Backend::readCanfdConfiguration(*session, {1}, cancelled);
                run["postStopRead"] = after.success;
                run["postStopConfigs"] = configsJson(after);
                run["postStopReadbackMatches"] = sameSettings(original, after);
                if (!sameSettings(original, after)) runFail(after.error.isEmpty() ? "Post-stop configuration readback mismatch" : after.error);
            } catch (const std::exception& ex) { runFail("Stop/readback exception: " + QString::fromUtf8(ex.what())); }
            catch (...) { runFail("Unknown stop/readback exception"); }
            run["success"] = runSuccess;
            runs.append(run);
            if (!run.value("preOpAndSync0ShutdownVerified").toBool()) break;
        }
    } catch (const std::exception& ex) { fail("Probe exception: " + QString::fromUtf8(ex.what())); }
    catch (...) { fail("Unknown probe exception"); }
    report["runs"] = runs;
    try { session->disconnectBus(); report["disconnected"] = true; }
    catch (const std::exception& ex) { report["disconnected"] = false; fail("Disconnect exception: " + QString::fromUtf8(ex.what())); }
    catch (...) { report["disconnected"] = false; fail("Unknown disconnect exception"); }
    report["errors"] = errors;
    report["success"] = success;
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (parser.isSet("output")) {
        QFile file(parser.value("output"));
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) {
            QTextStream(stderr) << "Could not write evidence file: " << file.errorString() << '\n';
            QTextStream(stdout) << json;
            return 2;
        }
    }
    QTextStream(stdout) << json;
    return success ? 0 : 1;
}
