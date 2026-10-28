#pragma once

#include <QString>

struct ValidationResult
{
    bool ok = false;
    QString error;
    QString trimmedName;
    QString normalizedUrl;
    QString id;
    QString icon;
    bool hasNameField = false;
    bool hasUrlField = false;
    bool hasIconField = false;
    bool hasOpenField = false;
    bool hasOpen = false;
};

class WebsiteValidator
{
public:
    static ValidationResult validateAddParams(const QString &name, const QString &url);
    static ValidationResult validateEditParams(const QString &jsonString);
    static bool validateId(const QString &id);
};
