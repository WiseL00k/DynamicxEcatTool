#include "EthercatMonitorController.h"

#include "EthercatMonitorWorker.h"

#include <QThread>

namespace Backend {
namespace {

constexpr int MonitorStopTimeoutMs = 1000;

} // namespace

EthercatMonitorController::EthercatMonitorController(QObject* parent)
    : QObject(parent)
{}

EthercatMonitorController::~EthercatMonitorController()
{
    stop();
}

bool EthercatMonitorController::isRunning() const
{
    return workerThread_ != nullptr && workerThread_->isRunning();
}

void EthercatMonitorController::startTest(std::shared_ptr<soem_interface::EcatMasterBus> master)
{
    if (!stop()) {
        emit logUpdated(QStringLiteral("监控线程停止超时，无法启动新的测试监控"));
        return;
    }

    workerThread_ = new QThread(this);
    testWorker_ = new EthercatTestWorker(std::move(master));
    testWorker_->moveToThread(workerThread_);

    connect(workerThread_, &QThread::started, testWorker_, &EthercatTestWorker::run);
    connect(testWorker_, &EthercatTestWorker::logUpdated, this, &EthercatMonitorController::logUpdated);
    connect(testWorker_, &EthercatTestWorker::finished, workerThread_, &QThread::quit);
    connect(testWorker_, &EthercatTestWorker::finished, testWorker_, &QObject::deleteLater);
    connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);
    connect(workerThread_, &QThread::finished, this, [this]() {
        testWorker_ = nullptr;
        workerThread_ = nullptr;
    });

    workerThread_->start();
}

void EthercatMonitorController::startCommunication(std::shared_ptr<soem_interface::EcatMasterBus> master)
{
    if (!stop()) {
        emit logUpdated(QStringLiteral("监控线程停止超时，无法启动新的通信监控"));
        return;
    }

    workerThread_ = new QThread(this);
    worker_ = new EthercatWorker(std::move(master));
    worker_->moveToThread(workerThread_);

    connect(worker_, &EthercatWorker::setDeviceOnlineStatus, this, &EthercatMonitorController::setDeviceOnlineStatus);
    connect(workerThread_, &QThread::started, worker_, &EthercatWorker::run);
    connect(worker_, &EthercatWorker::logUpdated, this, &EthercatMonitorController::logUpdated);
    connect(worker_, &EthercatWorker::finished, workerThread_, &QThread::quit);
    connect(worker_, &EthercatWorker::finished, this, &EthercatMonitorController::communicationMonitorStopped);
    connect(worker_, &EthercatWorker::finished, worker_, &QObject::deleteLater);
    connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);
    connect(workerThread_, &QThread::finished, this, [this]() {
        worker_ = nullptr;
        workerThread_ = nullptr;
    });

    workerThread_->start();
}

bool EthercatMonitorController::stop()
{
    if (testWorker_) {
        testWorker_->stop();
    }

    if (worker_) {
        worker_->stop();
    }

    QThread* thread = workerThread_;
    if (thread && thread->isRunning()) {
        thread->quit();
        if (!thread->wait(MonitorStopTimeoutMs)) {
            return false;
        }
    }

    testWorker_ = nullptr;
    worker_ = nullptr;
    workerThread_ = nullptr;
    return true;
}

} // namespace Backend
#include <utility>


