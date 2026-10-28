#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>

struct HistoryItem
{
    QString title;
    QString url;
    QString host;
    QDateTime visitedAt;
};

class HistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        UrlRole,
        HostRole,
        VisitedAtRole
    };
    Q_ENUM(Role)

    explicit HistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    Q_INVOKABLE void addVisit(const QString &title, const QString &url);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString urlAt(int row) const;
    Q_INVOKABLE QString titleAt(int row) const;
    Q_INVOKABLE QString hostAt(int row) const;
    Q_INVOKABLE QString visitedAt(int row) const;

signals:
    void countChanged();

private:
    void load();
    void save() const;
    int indexOfUrl(const QString &url) const;
    static QString normalizedUrl(const QString &url);

    QList<HistoryItem> m_items;
    int m_maxItems = 100;
};