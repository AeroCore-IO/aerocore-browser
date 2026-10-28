#include "bookmarkmodel.h"
#include "distributionprofile.h"

#include <QJsonArray>
#include <QSettings>
#include <QUrl>

namespace {
QList<BookmarkItem> builtInBookmarks()
{
    const QJsonArray configured = distributionProfile().value(QStringLiteral("bookmarks")).toArray();
    if (!configured.isEmpty()) {
        QList<BookmarkItem> bookmarks;
        for (const QJsonValue &value : configured) {
            const QJsonObject item = value.toObject();
            const QString title = item.value(QStringLiteral("title")).toString();
            const QString url = item.value(QStringLiteral("url")).toString();
            if (!title.isEmpty() && !url.isEmpty()) {
                bookmarks.append({title, url, {}, true});
            }
        }
        return bookmarks;
    }
    return {
        {QStringLiteral("Bilibili"), QStringLiteral("https://www.bilibili.com"), QStringLiteral("B"), true},
        {QStringLiteral("Youku"), QStringLiteral("https://www.youku.com"), QStringLiteral("Y"), true},
        {QStringLiteral("iQIYI"), QStringLiteral("https://www.iqiyi.com"), QStringLiteral("I"), true},
        {QStringLiteral("Tencent Video"), QStringLiteral("https://v.qq.com"), QStringLiteral("T"), true},
        {QStringLiteral("Mango TV"), QStringLiteral("https://www.mgtv.com"), QStringLiteral("M"), true},
        {QStringLiteral("咪咕视频"), QStringLiteral("https://www.miguvideo.com"), QStringLiteral("咪"), true},
        {QStringLiteral("Baidu"), QStringLiteral("https://www.baidu.com"), QStringLiteral("B"), true},
    };
}
}

BookmarkModel::BookmarkModel(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}

int BookmarkModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_items.size();
}

QVariant BookmarkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const BookmarkItem &item = m_items.at(index.row());
    switch (role) {
    case TitleRole:
        return item.title;
    case UrlRole:
        return item.url;
    case IconTextRole:
        return item.iconText;
    case PinnedRole:
        return item.pinned;
    default:
        return {};
    }
}

QHash<int, QByteArray> BookmarkModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {UrlRole, "url"},
        {IconTextRole, "iconText"},
        {PinnedRole, "pinned"},
    };
}

int BookmarkModel::count() const
{
    return m_items.size();
}

void BookmarkModel::addBookmark(const QString &title, const QString &url)
{
    const QString normalized = normalizedUrl(url);
    if (normalized.isEmpty() || indexOfUrl(normalized) >= 0) {
        return;
    }

    BookmarkItem item;
    item.title = title.trimmed().isEmpty() ? QUrl(normalized).host() : title.trimmed();
    item.url = normalized;
    item.iconText = iconTextForTitle(item.title, item.url);
    item.pinned = true;

    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(item);
    endInsertRows();
    emit countChanged();
    save();
}

void BookmarkModel::removeBookmark(const QString &url)
{
    const int row = indexOfUrl(normalizedUrl(url));
    if (row < 0) {
        return;
    }

    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    save();
}

void BookmarkModel::toggleBookmark(const QString &title, const QString &url)
{
    if (contains(url)) {
        removeBookmark(url);
        return;
    }

    addBookmark(title, url);
}

bool BookmarkModel::contains(const QString &url) const
{
    return indexOfUrl(normalizedUrl(url)) >= 0;
}

QString BookmarkModel::urlAt(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).url;
}

void BookmarkModel::load()
{
    beginResetModel();
    m_items.clear();

    QSettings settings;
    const int size = settings.beginReadArray(QStringLiteral("bookmarks/items"));
    for (int row = 0; row < size; ++row) {
        settings.setArrayIndex(row);
        BookmarkItem item;
        item.title = settings.value(QStringLiteral("title")).toString();
        item.url = normalizedUrl(settings.value(QStringLiteral("url")).toString());
        item.iconText = settings.value(QStringLiteral("iconText")).toString();
        item.pinned = settings.value(QStringLiteral("pinned"), true).toBool();
        if (!item.url.isEmpty()) {
            if (item.title.isEmpty()) {
                item.title = QUrl(item.url).host();
            }
            if (item.iconText.isEmpty()) {
                item.iconText = iconTextForTitle(item.title, item.url);
            }
            m_items.append(item);
        }
    }
    settings.endArray();

    // Import defaults only for a profile with no saved bookmark array. An
    // intentionally emptied list and an existing user's edits must survive.
    if (!settings.contains(QStringLiteral("bookmarks/items/size"))) {
        addDefaultBookmarks();
    }

    endResetModel();
    emit countChanged();
}

void BookmarkModel::save() const
{
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("bookmarks/items"));
    for (int row = 0; row < m_items.size(); ++row) {
        settings.setArrayIndex(row);
        const BookmarkItem &item = m_items.at(row);
        settings.setValue(QStringLiteral("title"), item.title);
        settings.setValue(QStringLiteral("url"), item.url);
        settings.setValue(QStringLiteral("iconText"), item.iconText);
        settings.setValue(QStringLiteral("pinned"), item.pinned);
    }
    settings.endArray();
}

void BookmarkModel::addDefaultBookmarks()
{
    m_items = builtInBookmarks();
    for (BookmarkItem &item : m_items) {
        item.iconText = iconTextForTitle(item.title, item.url);
    }
    save();
}

int BookmarkModel::indexOfUrl(const QString &url) const
{
    if (url.isEmpty()) {
        return -1;
    }

    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).url == url) {
            return row;
        }
    }
    return -1;
}

QString BookmarkModel::normalizedUrl(const QString &url)
{
    const QUrl normalized = QUrl::fromUserInput(url.trimmed());
    return normalized.isValid() ? normalized.toString(QUrl::RemoveFragment) : QString();
}

QString BookmarkModel::iconTextForTitle(const QString &title, const QString &url)
{
    const QString source = title.trimmed().isEmpty() ? QUrl(url).host() : title.trimmed();
    for (const QChar character : source) {
        if (character.isLetterOrNumber()) {
            return character.toUpper();
        }
    }
    return QStringLiteral("*");
}
