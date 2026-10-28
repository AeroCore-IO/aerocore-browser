#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

struct BookmarkItem
{
    QString title;
    QString url;
    QString iconText;
    bool pinned = true;
};

class BookmarkModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        UrlRole,
        IconTextRole,
        PinnedRole
    };
    Q_ENUM(Role)

    explicit BookmarkModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    Q_INVOKABLE void addBookmark(const QString &title, const QString &url);
    Q_INVOKABLE void removeBookmark(const QString &url);
    Q_INVOKABLE void toggleBookmark(const QString &title, const QString &url);
    Q_INVOKABLE bool contains(const QString &url) const;
    Q_INVOKABLE QString urlAt(int row) const;

signals:
    void countChanged();

private:
    void load();
    void save() const;
    void addDefaultBookmarks();
    int indexOfUrl(const QString &url) const;
    static QString normalizedUrl(const QString &url);
    static QString iconTextForTitle(const QString &title, const QString &url);

    QList<BookmarkItem> m_items;
};