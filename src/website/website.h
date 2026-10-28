#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

struct Website
{
    QString id;
    QString icon;
    QString name;
    QString url;
    QDateTime createdAt;
    QDateTime updatedAt;
    bool hasOpen = false;
    bool isCurrent = false;

    QJsonObject toJsonObject() const;
    static Website fromJsonObject(const QJsonObject &obj);
};

constexpr int kWebsiteNameMaxLength = 20;

QString normalizeWebsiteUrl(const QString &input);
bool isValidWebsiteUrl(const QString &input, QString *normalizedUrl = nullptr);
bool isValidWebsiteIcon(const QString &icon);
