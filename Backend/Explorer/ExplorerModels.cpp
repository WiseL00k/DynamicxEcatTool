#include "Backend/Explorer/ExplorerModels.h"

#include <algorithm>
#include <utility>

namespace explorer {
namespace {

bool validIndex(const QModelIndex& index, qsizetype size)
{
    return index.isValid() && index.row() >= 0 && index.row() < size;
}

bool readable(AccessMode access)
{
    return access == AccessMode::ReadOnly || access == AccessMode::ReadWrite;
}

bool writable(AccessMode access)
{
    return access == AccessMode::WriteOnly || access == AccessMode::ReadWrite;
}

} // namespace

ExplorerSlaveListModel::ExplorerSlaveListModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int ExplorerSlaveListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : items_.size();
}

QVariant ExplorerSlaveListModel::data(const QModelIndex& index, int role) const
{
    if (!validIndex(index, items_.size())) {
        return {};
    }
    const SlaveSnapshot& item = items_.at(index.row());
    switch (role) {
    case AddressRole: return item.address;
    case NameRole: return item.name;
    case VendorIdRole: return item.vendorId;
    case VendorIdTextRole: return hexValue(item.vendorId, 8);
    case ProductCodeRole: return item.productCode;
    case ProductCodeTextRole: return hexValue(item.productCode, 8);
    case RevisionNoRole: return item.revisionNo;
    case RevisionNoTextRole: return hexValue(item.revisionNo, 8);
    case SerialNumberRole: return item.serialNumber;
    case StateRole: return item.state;
    case StateTextRole: return item.stateText;
    case AlStatusCodeRole: return item.alStatusCode;
    case AlStatusTextRole: return item.alStatusText;
    case InputBitsRole: return item.inputBits;
    case OutputBitsRole: return item.outputBits;
    case EsiMatchedRole: return item.esiMatched;
    case EsiTrustedRole: return item.esiTrusted;
    case EsiPathRole: return item.esiPath;
    default: return {};
    }
}

QHash<int, QByteArray> ExplorerSlaveListModel::roleNames() const
{
    return {{AddressRole, "address"},
            {NameRole, "name"},
            {VendorIdRole, "vendorId"},
            {VendorIdTextRole, "vendorIdText"},
            {ProductCodeRole, "productCode"},
            {ProductCodeTextRole, "productCodeText"},
            {RevisionNoRole, "revisionNo"},
            {RevisionNoTextRole, "revisionNoText"},
            {SerialNumberRole, "serialNumber"},
            {StateRole, "state"},
            {StateTextRole, "stateText"},
            {AlStatusCodeRole, "alStatusCode"},
            {AlStatusTextRole, "alStatusText"},
            {InputBitsRole, "inputBits"},
            {OutputBitsRole, "outputBits"},
            {EsiMatchedRole, "esiMatched"},
            {EsiTrustedRole, "esiTrusted"},
            {EsiPathRole, "esiPath"}};
}

int ExplorerSlaveListModel::count() const
{
    return items_.size();
}

void ExplorerSlaveListModel::setItems(QVector<SlaveSnapshot> items)
{
    const bool changedCount = items_.size() != items.size();
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
    if (changedCount) {
        emit countChanged();
    }
}

void ExplorerSlaveListModel::clear()
{
    setItems({});
}

const QVector<SlaveSnapshot>& ExplorerSlaveListModel::items() const
{
    return items_;
}

const SlaveSnapshot* ExplorerSlaveListModel::itemAt(int row) const
{
    return row >= 0 && row < items_.size() ? &items_.at(row) : nullptr;
}

ExplorerPdoVariableModel::ExplorerPdoVariableModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int ExplorerPdoVariableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : items_.size();
}

QVariant ExplorerPdoVariableModel::data(const QModelIndex& index, int role) const
{
    if (!validIndex(index, items_.size())) {
        return {};
    }
    const PdoVariable& item = items_.at(index.row());
    switch (role) {
    case StableIdRole: return item.stableId;
    case SlaveAddressRole: return item.slaveAddress;
    case DirectionRole: return pdoDirectionText(item.direction);
    case PdoIndexRole: return item.pdoIndex;
    case PdoIndexTextRole: return hexValue(item.pdoIndex, 4);
    case PdoNameRole: return item.pdoName;
    case IndexRole: return item.index;
    case IndexTextRole: return hexValue(item.index, 4);
    case SubIndexRole: return item.subIndex;
    case SubIndexTextRole: return hexValue(item.subIndex, 2);
    case NameRole: return item.name;
    case DataTypeRole: return item.dataType;
    case BitLengthRole: return item.bitLength;
    case BitOffsetRole: return QVariant::fromValue<qlonglong>(item.bitOffset);
    case ValueRole: return item.value;
    case DisplayValueRole: return item.displayValue;
    case WritableRole: return item.writable;
    default: return {};
    }
}

QHash<int, QByteArray> ExplorerPdoVariableModel::roleNames() const
{
    return {{StableIdRole, "stableId"},
            {SlaveAddressRole, "slaveAddress"},
            {DirectionRole, "direction"},
            {PdoIndexRole, "pdoIndex"},
            {PdoIndexTextRole, "pdoIndexText"},
            {PdoNameRole, "pdoName"},
            {IndexRole, "index"},
            {IndexTextRole, "indexText"},
            {SubIndexRole, "subIndex"},
            {SubIndexTextRole, "subIndexText"},
            {NameRole, "name"},
            {DataTypeRole, "dataType"},
            {BitLengthRole, "bitLength"},
            {BitOffsetRole, "bitOffset"},
            {ValueRole, "value"},
            {DisplayValueRole, "displayValue"},
            {WritableRole, "writable"}};
}

int ExplorerPdoVariableModel::count() const
{
    return items_.size();
}

void ExplorerPdoVariableModel::setItems(QVector<PdoVariable> items)
{
    const bool changedCount = items_.size() != items.size();
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
    if (changedCount) {
        emit countChanged();
    }
}

void ExplorerPdoVariableModel::clear()
{
    setItems({});
}

bool ExplorerPdoVariableModel::updateValue(const QString& stableId,
                                           const QVariant& value,
                                           const QString& displayValue)
{
    for (qsizetype row = 0; row < items_.size(); ++row) {
        PdoVariable& item = items_[row];
        if (item.stableId != stableId) {
            continue;
        }
        item.value = value;
        item.displayValue = displayValue;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed, {ValueRole, DisplayValueRole});
        return true;
    }
    return false;
}

const QVector<PdoVariable>& ExplorerPdoVariableModel::items() const
{
    return items_;
}

const PdoVariable* ExplorerPdoVariableModel::itemByStableId(const QString& stableId) const
{
    for (const PdoVariable& item : items_) {
        if (item.stableId == stableId) {
            return &item;
        }
    }
    return nullptr;
}

ExplorerPdoVariableGroupModel::ExplorerPdoVariableGroupModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int ExplorerPdoVariableGroupModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : groups_.size();
}

QVariant ExplorerPdoVariableGroupModel::data(const QModelIndex& index, int role) const
{
    if (!validIndex(index, groups_.size())) {
        return {};
    }
    const Group& group = groups_.at(index.row());
    const PdoVariable* selected = selectedVariable(group);
    switch (role) {
    case GroupIdRole: return group.groupId;
    case DirectionRole: return pdoDirectionText(group.direction);
    case PdoIndexTextRole: return hexValue(group.pdoIndex, 4);
    case PdoNameRole: return group.pdoName;
    case IndexTextRole: return hexValue(group.index, 4);
    case NameRole: return group.name;
    case IsArrayRole: return group.isArray;
    case ElementLabelsRole: return group.elementLabels;
    case ElementCountRole: return group.elements.size();
    case SelectedElementIndexRole: return group.selectedElementIndex;
    case SelectedStableIdRole: return selected != nullptr ? selected->stableId : QString{};
    case SelectedSubIndexTextRole:
        return selected != nullptr ? hexValue(selected->subIndex, 2) : QString{};
    case SelectedDataTypeRole: return selected != nullptr ? selected->dataType : QString{};
    case SelectedDisplayValueRole:
        return selected != nullptr ? selected->displayValue : QString{};
    case SelectedWritableRole:
        return selected != nullptr && group.writeTrusted
            && selected->direction == PdoDirection::Rx && selected->writable;
    default: return {};
    }
}

QHash<int, QByteArray> ExplorerPdoVariableGroupModel::roleNames() const
{
    return {{GroupIdRole, "groupId"},
            {DirectionRole, "direction"},
            {PdoIndexTextRole, "pdoIndexText"},
            {PdoNameRole, "pdoName"},
            {IndexTextRole, "indexText"},
            {NameRole, "name"},
            {IsArrayRole, "isArray"},
            {ElementLabelsRole, "elementLabels"},
            {ElementCountRole, "elementCount"},
            {SelectedElementIndexRole, "selectedElementIndex"},
            {SelectedStableIdRole, "selectedStableId"},
            {SelectedSubIndexTextRole, "selectedSubIndexText"},
            {SelectedDataTypeRole, "selectedDataType"},
            {SelectedDisplayValueRole, "selectedDisplayValue"},
            {SelectedWritableRole, "selectedWritable"}};
}

int ExplorerPdoVariableGroupModel::count() const
{
    return groups_.size();
}

void ExplorerPdoVariableGroupModel::setVariables(
    QVector<PdoVariable> variables, bool arrayMetadataTrusted)
{
    QHash<QString, QString> previousSelections;
    for (const Group& group : std::as_const(groups_)) {
        const PdoVariable* selected = selectedVariable(group);
        if (selected != nullptr) {
            previousSelections.insert(group.groupId, selected->stableId);
        }
    }

    QVector<Group> groups;
    QVector<bool> consumed(variables.size(), false);
    for (qsizetype row = 0; row < variables.size(); ++row) {
        if (consumed.at(row)) {
            continue;
        }
        const PdoVariable& first = variables.at(row);
        QVector<qsizetype> candidateRows;
        bool validArray = arrayMetadataTrusted
            && !first.arrayName.isEmpty()
            && first.arrayElements > 0
            && first.arrayElementIndex >= 0
            && first.arrayElementIndex < first.arrayElements;

        if (validArray) {
            const QString candidateId = arrayGroupId(first);
            QVector<bool> seen(first.arrayElements, false);
            for (qsizetype candidateRow = 0;
                 candidateRow < variables.size(); ++candidateRow) {
                const PdoVariable& candidate = variables.at(candidateRow);
                if (arrayGroupId(candidate) != candidateId) {
                    continue;
                }
                const bool metadataMatches = candidate.arrayName == first.arrayName
                    && candidate.arrayLowerBound == first.arrayLowerBound
                    && candidate.arrayElements == first.arrayElements
                    && candidate.arrayElementIndex >= 0
                    && candidate.arrayElementIndex < first.arrayElements
                    && candidate.subIndex
                        == first.arrayLowerBound + candidate.arrayElementIndex;
                if (!metadataMatches || seen.at(candidate.arrayElementIndex)) {
                    validArray = false;
                    break;
                }
                seen[candidate.arrayElementIndex] = true;
                candidateRows.push_back(candidateRow);
            }
            validArray = validArray
                && candidateRows.size() == first.arrayElements
                && std::all_of(seen.cbegin(), seen.cend(), [](bool present) {
                       return present;
                   });
        }

        Group group;
        group.writeTrusted = arrayMetadataTrusted;
        if (validArray) {
            std::sort(candidateRows.begin(), candidateRows.end(),
                      [&variables](qsizetype lhs, qsizetype rhs) {
                          return variables.at(lhs).arrayElementIndex
                              < variables.at(rhs).arrayElementIndex;
                      });
            group.groupId = arrayGroupId(first);
            group.direction = first.direction;
            group.pdoIndex = first.pdoIndex;
            group.pdoName = first.pdoName;
            group.index = first.index;
            group.name = first.arrayName;
            group.isArray = true;
            for (qsizetype candidateRow : std::as_const(candidateRows)) {
                consumed[candidateRow] = true;
                group.elements.push_back(variables.at(candidateRow));
                group.elementLabels.push_back(elementLabel(variables.at(candidateRow)));
            }
        } else {
            consumed[row] = true;
            group.groupId = scalarGroupId(first);
            group.direction = first.direction;
            group.pdoIndex = first.pdoIndex;
            group.pdoName = first.pdoName;
            group.index = first.index;
            group.name = first.name;
            group.elementLabels = {first.name};
            group.elements = {first};
        }

        const QString previousStableId = previousSelections.value(group.groupId);
        for (qsizetype element = 0; element < group.elements.size(); ++element) {
            if (group.elements.at(element).stableId == previousStableId) {
                group.selectedElementIndex = element;
                break;
            }
        }
        groups.push_back(std::move(group));
    }

    const bool changedCount = groups_.size() != groups.size();
    beginResetModel();
    groups_ = std::move(groups);
    endResetModel();
    if (changedCount) {
        emit countChanged();
    }
}

void ExplorerPdoVariableGroupModel::clear()
{
    setVariables({}, false);
}

bool ExplorerPdoVariableGroupModel::selectElement(
    const QString& groupId, int elementIndex)
{
    for (qsizetype row = 0; row < groups_.size(); ++row) {
        Group& group = groups_[row];
        if (group.groupId != groupId) {
            continue;
        }
        if (elementIndex < 0 || elementIndex >= group.elements.size()) {
            return false;
        }
        if (group.selectedElementIndex == elementIndex) {
            return true;
        }
        group.selectedElementIndex = elementIndex;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed,
                         {SelectedElementIndexRole,
                          SelectedStableIdRole,
                          SelectedSubIndexTextRole,
                          SelectedDataTypeRole,
                          SelectedDisplayValueRole,
                          SelectedWritableRole});
        return true;
    }
    return false;
}

bool ExplorerPdoVariableGroupModel::updateValue(
    const QString& stableId, const QVariant& value, const QString& displayValue)
{
    for (qsizetype row = 0; row < groups_.size(); ++row) {
        Group& group = groups_[row];
        for (qsizetype element = 0; element < group.elements.size(); ++element) {
            PdoVariable& variable = group.elements[element];
            if (variable.stableId != stableId) {
                continue;
            }
            variable.value = value;
            variable.displayValue = displayValue;
            if (group.selectedElementIndex == element) {
                const QModelIndex changed = index(row);
                emit dataChanged(changed, changed, {SelectedDisplayValueRole});
            }
            return true;
        }
    }
    return false;
}

QString ExplorerPdoVariableGroupModel::arrayGroupId(const PdoVariable& variable)
{
    return QStringLiteral("pdo-group:%1:%2:%3:%4")
        .arg(variable.slaveAddress)
        .arg(static_cast<int>(variable.direction))
        .arg(variable.pdoIndex)
        .arg(variable.index);
}

QString ExplorerPdoVariableGroupModel::scalarGroupId(const PdoVariable& variable)
{
    return QStringLiteral("pdo-scalar:%1").arg(variable.stableId);
}

QString ExplorerPdoVariableGroupModel::elementLabel(const PdoVariable& variable)
{
    const QString name = variable.name.trimmed();
    const bool genericName = name.isEmpty()
        || name.compare(QStringLiteral("New array subitem"), Qt::CaseInsensitive) == 0;
    if (!genericName) {
        return name;
    }
    return QStringLiteral("元素 %1 · %2")
        .arg(variable.arrayElementIndex + 1)
        .arg(hexValue(variable.subIndex, 2));
}

const PdoVariable* ExplorerPdoVariableGroupModel::selectedVariable(
    const Group& group) const
{
    return group.selectedElementIndex >= 0
            && group.selectedElementIndex < group.elements.size()
        ? &group.elements.at(group.selectedElementIndex) : nullptr;
}

ExplorerPdoMappingModel::ExplorerPdoMappingModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int ExplorerPdoMappingModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : items_.size();
}

QVariant ExplorerPdoMappingModel::data(const QModelIndex& index, int role) const
{
    if (!validIndex(index, items_.size())) {
        return {};
    }
    const PdoMappingItem& item = items_.at(index.row());
    switch (role) {
    case StableIdRole: return item.stableId;
    case SlaveAddressRole: return item.slaveAddress;
    case DirectionRole: return pdoDirectionText(item.direction);
    case PdoIndexRole: return item.pdoIndex;
    case PdoIndexTextRole: return hexValue(item.pdoIndex, 4);
    case PdoNameRole: return item.pdoName;
    case SyncManagerRole: return item.syncManager;
    case FixedRole: return item.fixed;
    case MandatoryRole: return item.mandatory;
    case IndexRole: return item.index;
    case IndexTextRole: return hexValue(item.index, 4);
    case SubIndexRole: return item.subIndex;
    case SubIndexTextRole: return hexValue(item.subIndex, 2);
    case NameRole: return item.name;
    case DataTypeRole: return item.dataType;
    case BitLengthRole: return item.bitLength;
    case PdoBitOffsetRole: return QVariant::fromValue<qlonglong>(item.pdoBitOffset);
    case ProcessBitOffsetRole: return QVariant::fromValue<qlonglong>(item.processBitOffset);
    default: return {};
    }
}

QHash<int, QByteArray> ExplorerPdoMappingModel::roleNames() const
{
    return {{StableIdRole, "stableId"},
            {SlaveAddressRole, "slaveAddress"},
            {DirectionRole, "direction"},
            {PdoIndexRole, "pdoIndex"},
            {PdoIndexTextRole, "pdoIndexText"},
            {PdoNameRole, "pdoName"},
            {SyncManagerRole, "syncManager"},
            {FixedRole, "fixed"},
            {MandatoryRole, "mandatory"},
            {IndexRole, "index"},
            {IndexTextRole, "indexText"},
            {SubIndexRole, "subIndex"},
            {SubIndexTextRole, "subIndexText"},
            {NameRole, "name"},
            {DataTypeRole, "dataType"},
            {BitLengthRole, "bitLength"},
            {PdoBitOffsetRole, "pdoBitOffset"},
            {ProcessBitOffsetRole, "processBitOffset"}};
}

int ExplorerPdoMappingModel::count() const
{
    return items_.size();
}

void ExplorerPdoMappingModel::setItems(QVector<PdoMappingItem> items)
{
    const bool changedCount = items_.size() != items.size();
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
    if (changedCount) {
        emit countChanged();
    }
}

void ExplorerPdoMappingModel::setMappings(quint16 slaveAddress,
                                          const QVector<PdoMapping>& mappings)
{
    QVector<PdoMappingItem> items;
    for (const PdoMapping& mapping : mappings) {
        for (const PdoEntry& entry : mapping.entries) {
            PdoMappingItem item;
            item.stableId = makePdoStableId(slaveAddress,
                                            mapping.direction,
                                            mapping.index,
                                            entry.index,
                                            entry.subIndex);
            item.stableId += QStringLiteral(":%1").arg(entry.processBitOffset);
            item.slaveAddress = slaveAddress;
            item.direction = mapping.direction;
            item.pdoIndex = mapping.index;
            item.pdoName = mapping.name;
            item.syncManager = mapping.syncManager;
            item.fixed = mapping.fixed;
            item.mandatory = mapping.mandatory;
            item.index = entry.index;
            item.subIndex = entry.subIndex;
            item.name = entry.name;
            item.dataType = entry.dataType;
            item.bitLength = entry.bitLength;
            item.pdoBitOffset = entry.pdoBitOffset;
            item.processBitOffset = entry.processBitOffset;
            items.push_back(std::move(item));
        }
    }
    setItems(std::move(items));
}

void ExplorerPdoMappingModel::clear()
{
    setItems({});
}

const QVector<PdoMappingItem>& ExplorerPdoMappingModel::items() const
{
    return items_;
}

ExplorerObjectDictionaryModel::ExplorerObjectDictionaryModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int ExplorerObjectDictionaryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : items_.size();
}

QVariant ExplorerObjectDictionaryModel::data(const QModelIndex& index, int role) const
{
    if (!validIndex(index, items_.size())) {
        return {};
    }
    const ObjectDictionaryItem& item = items_.at(index.row());
    switch (role) {
    case StableIdRole: return item.stableId;
    case SlaveAddressRole: return item.slaveAddress;
    case IndexRole: return item.index;
    case IndexTextRole: return hexValue(item.index, 4);
    case SubIndexRole: return item.subIndex;
    case SubIndexTextRole: return hexValue(item.subIndex, 2);
    case NameRole: return item.name;
    case DataTypeRole: return item.dataType;
    case BitLengthRole: return item.bitLength;
    case AccessRole: return accessModeText(item.access);
    case PdoMappingRole: return item.pdoMapping;
    case ValueRole: return item.value;
    case DisplayValueRole: return item.displayValue;
    case ReadableRole: return readable(item.access);
    case WritableRole: return writable(item.access);
    default: return {};
    }
}

QHash<int, QByteArray> ExplorerObjectDictionaryModel::roleNames() const
{
    return {{StableIdRole, "stableId"},
            {SlaveAddressRole, "slaveAddress"},
            {IndexRole, "index"},
            {IndexTextRole, "indexText"},
            {SubIndexRole, "subIndex"},
            {SubIndexTextRole, "subIndexText"},
            {NameRole, "name"},
            {DataTypeRole, "dataType"},
            {BitLengthRole, "bitLength"},
            {AccessRole, "access"},
            {PdoMappingRole, "pdoMapping"},
            {ValueRole, "value"},
            {DisplayValueRole, "displayValue"},
            {ReadableRole, "readable"},
            {WritableRole, "writable"}};
}

int ExplorerObjectDictionaryModel::count() const
{
    return items_.size();
}

void ExplorerObjectDictionaryModel::setItems(QVector<ObjectDictionaryItem> items)
{
    const bool changedCount = items_.size() != items.size();
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
    if (changedCount) {
        emit countChanged();
    }
}

void ExplorerObjectDictionaryModel::setObjects(
    quint16 slaveAddress,
    const QVector<ObjectDictionaryEntry>& objects)
{
    QVector<ObjectDictionaryItem> items;
    for (const ObjectDictionaryEntry& object : objects) {
        if (object.subItems.isEmpty()) {
            ObjectDictionaryItem item;
            item.stableId = makeSdoStableId(slaveAddress, object.index, 0);
            item.slaveAddress = slaveAddress;
            item.index = object.index;
            item.name = object.name;
            item.dataType = object.dataType;
            item.bitLength = object.bitSize;
            item.access = object.access;
            item.pdoMapping = object.pdoMapping;
            item.value = object.defaultValue;
            item.displayValue = object.defaultValue;
            items.push_back(std::move(item));
            continue;
        }

        for (const OdSubItem& subItem : object.subItems) {
            ObjectDictionaryItem item;
            item.stableId = makeSdoStableId(slaveAddress, object.index, subItem.subIndex);
            item.slaveAddress = slaveAddress;
            item.index = object.index;
            item.subIndex = subItem.subIndex;
            item.name = subItem.name.isEmpty() ? object.name : subItem.name;
            item.dataType = subItem.dataType;
            item.bitLength = subItem.bitSize;
            item.access = subItem.access == AccessMode::None ? object.access : subItem.access;
            item.pdoMapping = subItem.pdoMapping.isEmpty() ? object.pdoMapping
                                                          : subItem.pdoMapping;
            item.value = subItem.defaultValue;
            item.displayValue = subItem.defaultValue;
            items.push_back(std::move(item));
        }
    }
    setItems(std::move(items));
}

void ExplorerObjectDictionaryModel::clear()
{
    setItems({});
}

bool ExplorerObjectDictionaryModel::updateValue(const QString& stableId,
                                                const QVariant& value,
                                                const QString& displayValue)
{
    for (qsizetype row = 0; row < items_.size(); ++row) {
        ObjectDictionaryItem& item = items_[row];
        if (item.stableId != stableId) {
            continue;
        }
        item.value = value;
        item.displayValue = displayValue;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed, {ValueRole, DisplayValueRole});
        return true;
    }
    return false;
}

const QVector<ObjectDictionaryItem>& ExplorerObjectDictionaryModel::items() const
{
    return items_;
}

const ObjectDictionaryItem* ExplorerObjectDictionaryModel::itemByStableId(
    const QString& stableId) const
{
    for (const ObjectDictionaryItem& item : items_) {
        if (item.stableId == stableId) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace explorer
