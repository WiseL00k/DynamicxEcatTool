#include "CanFrameModel.h"
#include <algorithm>

namespace Backend {
QVariant CanFrameModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || size_t(index.row()) >= records_.size()) return {};
    const auto& r = records_[index.row()];
    switch (role) {
    case TimestampRole: {
        const qint64 seconds = r.timestampUs / 1000000;
        return QStringLiteral("%1:%2:%3.%4").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
            .arg(r.timestampUs % 1000000, 6, 10, QLatin1Char('0'));
    }
    case SlaveRole: return r.frame.slave;
    case ChannelRole: return r.frame.channel;
    case IdHexRole: return canfd::hex(r.frame.id, 3);
    case DlcRole: return r.frame.dlc;
    case DataHexRole: return canfd::bytesHex(r.frame.data.data(), r.frame.dlc);
    default: return {};
    }
}

QHash<int, QByteArray> CanFrameModel::roleNames() const
{ return {{TimestampRole,"timestamp"},{SlaveRole,"slave"},{ChannelRole,"channel"},{IdHexRole,"idHex"},{DlcRole,"dlc"},{DataHexRole,"dataHex"}}; }

quint64 CanFrameModel::append(std::deque<CanFrameRecord> batch)
{
    quint64 dropped = 0;
    while (batch.size() > Capacity) { batch.pop_front(); ++dropped; }
    const size_t remove = records_.size() + batch.size() > Capacity ? records_.size() + batch.size() - Capacity : 0;
    if (remove) {
        beginRemoveRows({}, 0, int(remove - 1));
        for (size_t i = 0; i < remove; ++i) records_.pop_front();
        endRemoveRows(); dropped += remove;
    }
    if (!batch.empty()) {
        beginInsertRows({}, int(records_.size()), int(records_.size() + batch.size() - 1));
        for (auto& record : batch) records_.push_back(std::move(record));
        endInsertRows();
    }
    return dropped;
}

void CanFrameModel::clear()
{ if (!records_.empty()) { beginResetModel(); records_.clear(); endResetModel(); } }
} // namespace Backend
