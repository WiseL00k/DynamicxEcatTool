#pragma once

#include <QAbstractListModel>
#include <vector>

struct MotorItem
{
    QString type;
    QString name;
    bool online{false};
    int canBus{0};
    int canId{0};
    QString slaveName;
};

class DeviceStatusModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        TypeRole = Qt::UserRole + 1,
        NameRole,
        OnlineRole,
        CanBusRole,
        CanIdRole,
        SlaveNameRole
    };

    explicit DeviceStatusModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid()) {
            return 0;
        }

        return static_cast<int>(items_.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
            return {};
        }

        const auto& item = items_[static_cast<size_t>(index.row())];

        switch (role) {
        case TypeRole:
            return item.type;
        case NameRole:
            return item.name;
        case OnlineRole:
            return item.online;
        case CanBusRole:
            return item.canBus;
        case CanIdRole:
            return item.canId;
        case SlaveNameRole:
            return item.slaveName;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {TypeRole, "type"},
            {NameRole, "name"},
            {OnlineRole, "online"},
            {CanBusRole, "canBus"},
            {CanIdRole, "canId"},
            {SlaveNameRole, "slaveName"}
        };
    }

    void addSlaveHeader(const QString& name)
    {
        beginInsertRows(QModelIndex(), rowCount(), rowCount());

        MotorItem item;
        item.type = QStringLiteral("slaveHeader");
        item.slaveName = name;
        items_.push_back(item);

        endInsertRows();
    }

    void addImu(const QString& name, int bus)
    {
        beginInsertRows(QModelIndex(), rowCount(), rowCount());

        MotorItem item;
        item.type = QStringLiteral("imu");
        item.name = name;
        item.canBus = bus;
        items_.push_back(item);

        endInsertRows();
    }

    void addMotor(const QString& name, int bus, int id)
    {
        beginInsertRows(QModelIndex(), rowCount(), rowCount());

        MotorItem item;
        item.type = QStringLiteral("motor");
        item.name = name;
        item.canBus = bus;
        item.canId = id;
        items_.push_back(item);

        endInsertRows();
    }

    bool setDeviceOnline(const QString& name, bool status)
    {
        for (int i = 0; i < rowCount(); ++i) {
            auto& item = items_[static_cast<size_t>(i)];
            if (item.name != name) {
                continue;
            }

            if (item.online != status) {
                item.online = status;
                const QModelIndex changedIndex = index(i);
                emit dataChanged(changedIndex, changedIndex, {OnlineRole});
            }

            return true;
        }

        return false;
    }

    void clear()
    {
        beginResetModel();
        items_.clear();
        endResetModel();
    }

    void clearAllOnline()
    {
        bool changed = false;

        for (auto& item : items_) {
            if ((item.type == QStringLiteral("motor") || item.type == QStringLiteral("imu")) && item.online) {
                item.online = false;
                changed = true;
            }
        }

        if (changed && !items_.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1), {OnlineRole});
        }
    }

private:
    std::vector<MotorItem> items_;
};
