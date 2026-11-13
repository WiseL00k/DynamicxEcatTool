#include <QtTest>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

extern "C" {
#include "soem/soem.h"
}

namespace {
class PortFixture {
public:
    std::unique_ptr<ecx_portt> port = std::make_unique<ecx_portt>();
    PortFixture() {
#ifdef _WIN32
        InitializeCriticalSection(&port->rx_mutex);
        InitializeCriticalSection(&port->tx_mutex);
        InitializeCriticalSection(&port->getindex_mutex);
#else
        pthread_mutex_init(&port->rx_mutex, nullptr);
        pthread_mutex_init(&port->tx_mutex, nullptr);
        pthread_mutex_init(&port->getindex_mutex, nullptr);
#endif
        port->stack.sock = &port->sockhandle;
        port->stack.txbuf = &port->txbuf;
        port->stack.txbuflength = &port->txbuflength;
        port->stack.tempbuf = &port->tempinbuf;
        port->stack.rxbuf = &port->rxbuf;
        port->stack.rxbufstat = &port->rxbufstat;
        port->stack.rxsa = &port->rxsa;
    }
    ~PortFixture() {
#ifdef _WIN32
        DeleteCriticalSection(&port->getindex_mutex);
        DeleteCriticalSection(&port->tx_mutex);
        DeleteCriticalSection(&port->rx_mutex);
#else
        pthread_mutex_destroy(&port->getindex_mutex);
        pthread_mutex_destroy(&port->tx_mutex);
        pthread_mutex_destroy(&port->rx_mutex);
#endif
    }
    void lock() {
#ifdef _WIN32
        EnterCriticalSection(&port->rx_mutex);
#else
        pthread_mutex_lock(&port->rx_mutex);
#endif
    }
    void unlock() {
#ifdef _WIN32
        LeaveCriticalSection(&port->rx_mutex);
#else
        pthread_mutex_unlock(&port->rx_mutex);
#endif
    }
    void publish(unsigned idx, unsigned wkc) {
        port->rxbuf[idx][0] = 2;
        port->rxbuf[idx][1] = 0;
        port->rxbuf[idx][2] = static_cast<uint8_t>(wkc);
        port->rxbuf[idx][3] = static_cast<uint8_t>(wkc >> 8);
        port->rxbufstat[idx] = EC_BUF_RCVD;
    }
};
}

class SoemPortTests : public QObject {
    Q_OBJECT
private slots:
    void bufferedReceiveWaitsForPublication() {
        PortFixture f;
        const auto idx = ecx_getindex(f.port.get());
        f.lock();
        f.publish(idx, 42);
        std::promise<void> started;
        auto ready = started.get_future();
        auto result = std::async(std::launch::async, [&] {
            started.set_value();
            return ecx_waitinframe(f.port.get(), idx, 10000);
        });
        ready.wait();
        const auto whilePublishing = result.wait_for(std::chrono::milliseconds(30));
        f.unlock();
        QCOMPARE(whilePublishing, std::future_status::timeout);
        QCOMPARE(result.get(), 42);
        ecx_setbufstat(f.port.get(), idx, EC_BUF_EMPTY);
    }
    void allocationWaitsForReceiveStatePublication() {
        PortFixture f;
        f.lock();
        std::promise<void> started;
        auto ready = started.get_future();
        auto result = std::async(std::launch::async, [&] {
            started.set_value();
            return ecx_getindex(f.port.get());
        });
        ready.wait();
        const auto whilePublishing = result.wait_for(std::chrono::milliseconds(30));
        f.unlock();
        QCOMPARE(whilePublishing, std::future_status::timeout);
        QVERIFY(result.get() < EC_MAXBUF);
    }
    void concurrentMailboxAndPdoKeepSeparateReplies() {
        PortFixture f;
        std::atomic<bool> good{true};
        std::atomic<unsigned> owners[EC_MAXBUF]{};
        auto exchange = [&](unsigned expected) {
            for (int cycle = 0; cycle != 5000; ++cycle) {
                const auto idx = ecx_getindex(f.port.get());
                if (owners[idx].fetch_add(1) != 0) good = false;
                f.lock();
                f.publish(idx, expected);
                f.unlock();
                if (ecx_waitinframe(f.port.get(), idx, 10000) != static_cast<int>(expected)) good = false;
                if (owners[idx].fetch_sub(1) != 1) good = false;
                ecx_setbufstat(f.port.get(), idx, EC_BUF_EMPTY);
            }
        };
        std::thread pdo(exchange, 3);
        std::thread mailbox(exchange, 1);
        pdo.join(); mailbox.join();
        QVERIFY(good.load());
    }
};
QTEST_GUILESS_MAIN(SoemPortTests)
#include "SoemPortTests.moc"
