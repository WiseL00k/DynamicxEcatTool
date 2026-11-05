#pragma once
#include "CanfdProtocol.h"
#include <QAbstractListModel>
#include <deque>

namespace Backend {
struct CanFrameRecord { qint64 timestampUs{0}; canfd::Frame frame; };

class CanFrameModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { TimestampRole = Qt::UserRole + 1, SlaveRole, ChannelRole, IdHexRole, DlcRole, DataHexRole };
    explicit CanFrameModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(records_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    quint64 append(std::deque<CanFrameRecord> batch);
    void clear();
    static constexpr size_t Capacity = 10000;
private:
    std::deque<CanFrameRecord> records_;
};
} // namespace Backend
