#pragma once
#include <QAbstractListModel>

struct MotorItem
{
    QString type;
    QString name;
    bool online;
    int canBus;
    int canId;
    QString slaveName;
};

struct ImuItem
{
    QString type;
    QString name;
    bool online;
    int canBus;
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

    explicit DeviceStatusModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return items_.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        const auto &m = items_[index.row()];

        switch(role)
        {
        case TypeRole: return m.type;
        case NameRole: return m.name;
        case OnlineRole: return m.online;
        case CanBusRole: return m.canBus;
        case CanIdRole: return m.canId;
        case SlaveNameRole: return m.slaveName;
        }

        return {};
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

public:

    void addSlaveHeader(const QString &name)
    {
        beginInsertRows(QModelIndex(), items_.size(), items_.size());

        MotorItem m;
        m.type = "slaveHeader";
        m.slaveName = name;

        items_.push_back(m);

        endInsertRows();
    }

    void addImu(const QString &name, int bus)
    {
        beginInsertRows(QModelIndex(), items_.size(), items_.size());

        MotorItem m;
        m.type = "imu";
        m.name = name;
        m.online = false;
        m.canBus = bus;

        items_.push_back(m);

        endInsertRows();
    }

    void addMotor(const QString &name, int bus, int id)
    {
        beginInsertRows(QModelIndex(), items_.size(), items_.size());

        MotorItem m;
        m.type = "motor";
        m.name = name;
        m.online = false;
        m.canBus = bus;
        m.canId = id;

        items_.push_back(m);

        endInsertRows();
    }

    bool setDeviceOnline(const QString &name, bool status)
    {
        for(int i = 0; i < items_.size(); ++i)
        {
            auto &m = items_[i];

            if(m.name == name)
            {
                if(m.online != status)
                {
                    m.online = status;

                    QModelIndex idx = index(i);
                    emit dataChanged(idx, idx, {OnlineRole});
                }

                return true;
            }
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

        for (auto &m : items_)
        {
            if (m.type == "motor" && m.online)
            {
                m.online = false;
                changed = true;
            }
        }

        if (changed && !items_.empty())
        {
            emit dataChanged(index(0), index(items_.size() - 1), {OnlineRole});
        }
    }

private:
    std::vector<MotorItem> items_;
};
