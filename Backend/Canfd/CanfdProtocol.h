#pragma once

#include "SOEM_interface/EcatMasterBus.h"
#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <array>
#include <vector>

namespace Backend::canfd {

constexpr int ImageBytes = 185;
constexpr int ChannelCount = 2;
constexpr int SlotsPerChannel = 9;
constexpr quint16 ValidMask = 0x0800;

struct Frame {
    int slave{0};
    int channel{0};
    quint16 id{0};
    int dlc{0};
    std::array<quint8, 8> data{};
};

struct ChannelConfig {
    int slave{0};
    int channel{0};
    bool enabled{true};
    int format{0};
    quint32 nominalBitrate{1000000};
    quint32 dataBitrate{5000000};
    quint16 options{5};
    quint8 status{0};
    quint32 error{0};
    bool verified{false};
    QVariantMap toMap() const;
};

QString hex(quint64 value, int width);
QString bytesHex(const quint8* bytes, int length);
bool parseFrame(int slave, int channel, const QString& id, int dlc,
                const QString& data, Frame& result, QString& error);
bool parseConfig(int slave, const QVariantMap& map, ChannelConfig& result, QString& error);
bool validConfig(const ChannelConfig& config, QString& error);
bool validateScan(const soem_interface::BusScanResult& scan, int expectedCount, QString& error);
void encodeFrame(std::vector<uint8_t>& image, int slot, const Frame& frame);
QVariantList decodeRows(const std::vector<uint8_t>& image, bool outputs);
std::vector<Frame> decodeValidFrames(int slave, const std::vector<uint8_t>& input);
QVariantMap decodeImage(const soem_interface::SlaveProcessImage& image);
quint32 readLe(const quint8* data, int count);
std::vector<quint8> writeLe(quint32 value, int count);

} // namespace Backend::canfd
