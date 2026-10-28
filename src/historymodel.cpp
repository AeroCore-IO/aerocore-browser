#include "historymodel.h"

#include <QSettings>
#include <QUrl>

HistoryModel::HistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_items.size();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const HistoryItem &item = m_items.at(index.row());
    switch (role) {
    case TitleRole:
        return item.title;
    case UrlRole:
        return item.url;
    case HostRole:
        return item.host;
    case VisitedAtRole:
        return item.visitedAt.toLocalTime().toString(QStringLiteral("MM-dd hh:mm"));
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {UrlRole, "url"},
        {HostRole, "host"},
        {VisitedAtRole, "visitedAt"},
    };
}

int HistoryModel::count() const
{
    return m_items.size();
}

void HistoryModel::addVisit(const QString &title, const QString &url)
{
    const QString normalized = normalizedUrl(url);
    if (normalized.isEmpty() || normalized.startsWith(QStringLiteral("about:"))) {
        return;
    }

    const int existingRow = indexOfUrl(normalized);
    if (existingRow >= 0) {
        beginRemoveRows({}, existingRow, existingRow);
        m_items.removeAt(existingRow);
        endRemoveRows();
    }

    HistoryItem item;
    item.url = normalized;
    item.host = QUrl(normalized).host();
    item.title = title.trimmed().isEmpty() ? item.host : title.trimmed();
    item.visitedAt = QDateTime::currentDateTimeUtc();

    beginInsertRows({}, 0, 0);
    m_items.prepend(item);
    endInsertRows();

    while (m_items.size() > m_maxItems) {
        beginRemoveRows({}, m_items.size() - 1, m_items.size() - 1);
        m_items.removeLast();
        endRemoveRows();
    }

    emit countChanged();
    save();
}

void HistoryModel::clear()
{
    if (m_items.isEmpty()) {
        return;
    }

    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
    save();
}

QString HistoryModel::urlAt(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).url;
}

QString HistoryModel::titleAt(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).title;
}

QString HistoryModel::hostAt(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).host;
}

QString HistoryModel::visitedAt(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).visitedAt.toLocalTime().toString(QStringLiteral("MM-dd hh:mm"));
}

void HistoryModel::load()
{
    beginResetModel();
    m_items.clear();

    QSettings settings;
    const int size = settings.beginReadArray(QStringLiteral("history/items"));
    for (int row = 0; row < size; ++row) {
        settings.setArrayIndex(row);
        HistoryItem item;
        item.title = settings.value(QStringLiteral("title")).toString();
        item.url = normalizedUrl(settings.value(QStringLiteral("url")).toString());
        item.host = settings.value(QStringLiteral("host")).toString();
        item.visitedAt = QDateTime::fromString(settings.value(QStringLiteral("visitedAt")).toString(), Qt::ISODate);
        if (!item.url.isEmpty()) {
            if (item.host.isEmpty()) {
                item.host = QUrl(item.url).host();
            }
            if (item.title.isEmpty()) {
                item.title = item.host;
            }
            if (!item.visitedAt.isValid()) {
                item.visitedAt = QDateTime::currentDateTimeUtc();
            }
            m_items.append(item);
        }
    }
    settings.endArray();

    endResetModel();
    emit countChanged();
}

void HistoryModel::save() const
{
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("history/items"));
    for (int row = 0; row < m_items.size(); ++row) {
        settings.setArrayIndex(row);
        const HistoryItem &item = m_items.at(row);
        settings.setValue(QStringLiteral("title"), item.title);
        settings.setValue(QStringLiteral("url"), item.url);
        settings.setValue(QStringLiteral("host"), item.host);
        settings.setValue(QStringLiteral("visitedAt"), item.visitedAt.toUTC().toString(Qt::ISODate));
    }
    settings.endArray();
}

int HistoryModel::indexOfUrl(const QString &url) const
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).url == url) {
            return row;
        }
    }
    return -1;
}

QString HistoryModel::normalizedUrl(const QString &url)
{
    const QUrl normalized = QUrl::fromUserInput(url.trimmed());
    return normalized.isValid() ? normalized.toString(QUrl::RemoveFragment) : QString();
}