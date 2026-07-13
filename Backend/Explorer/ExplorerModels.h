#pragma once

#include "Backend/Explorer/ExplorerTypes.h"

#include <QAbstractListModel>

namespace explorer {

class ExplorerSlaveListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        AddressRole = Qt::UserRole + 1,
        NameRole,
        VendorIdRole,
        VendorIdTextRole,
        ProductCodeRole,
        ProductCodeTextRole,
        RevisionNoRole,
        RevisionNoTextRole,
        SerialNumberRole,
        StateRole,
        StateTextRole,
        AlStatusCodeRole,
        AlStatusTextRole,
        InputBitsRole,
        OutputBitsRole,
        EsiMatchedRole,
        EsiTrustedRole,
        EsiPathRole
    };

    explicit ExplorerSlaveListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    void setItems(QVector<SlaveSnapshot> items);
    void clear();
    const QVector<SlaveSnapshot>& items() const;
    const SlaveSnapshot* itemAt(int row) const;

signals:
    void countChanged();

private:
    QVector<SlaveSnapshot> items_;
};

class ExplorerPdoVariableModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        StableIdRole = Qt::UserRole + 1,
        SlaveAddressRole,
        DirectionRole,
        PdoIndexRole,
        PdoIndexTextRole,
        PdoNameRole,
        IndexRole,
        IndexTextRole,
        SubIndexRole,
        SubIndexTextRole,
        NameRole,
        DataTypeRole,
        BitLengthRole,
        BitOffsetRole,
        ValueRole,
        DisplayValueRole,
        WritableRole
    };

    explicit ExplorerPdoVariableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    void setItems(QVector<PdoVariable> items);
    void clear();
    bool updateValue(const QString& stableId, const QVariant& value, const QString& displayValue);
    const QVector<PdoVariable>& items() const;
    const PdoVariable* itemByStableId(const QString& stableId) const;

signals:
    void countChanged();

private:
    QVector<PdoVariable> items_;
};

class ExplorerPdoMappingModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        StableIdRole = Qt::UserRole + 1,
        SlaveAddressRole,
        DirectionRole,
        PdoIndexRole,
        PdoIndexTextRole,
        PdoNameRole,
        SyncManagerRole,
        FixedRole,
        MandatoryRole,
        IndexRole,
        IndexTextRole,
        SubIndexRole,
        SubIndexTextRole,
        NameRole,
        DataTypeRole,
        BitLengthRole,
        PdoBitOffsetRole,
        ProcessBitOffsetRole
    };

    explicit ExplorerPdoMappingModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    void setItems(QVector<PdoMappingItem> items);
    void setMappings(quint16 slaveAddress, const QVector<PdoMapping>& mappings);
    void clear();
    const QVector<PdoMappingItem>& items() const;

signals:
    void countChanged();

private:
    QVector<PdoMappingItem> items_;
};

class ExplorerObjectDictionaryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        StableIdRole = Qt::UserRole + 1,
        SlaveAddressRole,
        IndexRole,
        IndexTextRole,
        SubIndexRole,
        SubIndexTextRole,
        NameRole,
        DataTypeRole,
        BitLengthRole,
        AccessRole,
        PdoMappingRole,
        ValueRole,
        DisplayValueRole,
        ReadableRole,
        WritableRole
    };

    explicit ExplorerObjectDictionaryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    void setItems(QVector<ObjectDictionaryItem> items);
    void setObjects(quint16 slaveAddress, const QVector<ObjectDictionaryEntry>& objects);
    void clear();
    bool updateValue(const QString& stableId, const QVariant& value, const QString& displayValue);
    const QVector<ObjectDictionaryItem>& items() const;
    const ObjectDictionaryItem* itemByStableId(const QString& stableId) const;

signals:
    void countChanged();

private:
    QVector<ObjectDictionaryItem> items_;
};

} // namespace explorer
