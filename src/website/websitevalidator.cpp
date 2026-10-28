#include "website/websitevalidator.h"

#include "website/website.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString validateName(const QString &name, QString *trimmedName)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("网站名称不能为空");
    }
    if (trimmed.length() > kWebsiteNameMaxLength) {
        return QStringLiteral("网站名称不能超过%1个字符").arg(kWebsiteNameMaxLength);
    }
    if (trimmedName) {
        *trimmedName = trimmed;
    }
    return {};
}

} // namespace

ValidationResult WebsiteValidator::validateAddParams(const QString &name, const QString &url)
{
    ValidationResult result;
    result.error = validateName(name, &result.trimmedName);
    if (!result.error.isEmpty()) {
        return result;
    }

    if (url.trimmed().isEmpty()) {
        result.error = QStringLiteral("网站地址不能为空");
        return result;
    }

    if (!isValidWebsiteUrl(url, &result.normalizedUrl)) {
        result.error = QStringLiteral("请输入有效的网址");
        return result;
    }

    result.ok = true;
    return result;
}

ValidationResult WebsiteValidator::validateEditParams(const QString &jsonString)
{
    ValidationResult result;
    if (jsonString.trimmed().isEmpty()) {
        result.error = QStringLiteral("参数无效");
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());
    if (!doc.isObject()) {
        result.error = QStringLiteral("参数无效");
        return result;
    }

    const QJsonObject obj = doc.object();
    result.id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (result.id.isEmpty()) {
        result.error = QStringLiteral("参数无效");
        return result;
    }

    if (obj.contains(QStringLiteral("name"))) {
        result.hasNameField = true;
        result.error = validateName(obj.value(QStringLiteral("name")).toString(), &result.trimmedName);
        if (!result.error.isEmpty()) {
            return result;
        }
    }

    if (obj.contains(QStringLiteral("url"))) {
        result.hasUrlField = true;
        if (obj.value(QStringLiteral("url")).toString().trimmed().isEmpty()) {
            result.error = QStringLiteral("网站地址不能为空");
            return result;
        }
        if (!isValidWebsiteUrl(obj.value(QStringLiteral("url")).toString(), &result.normalizedUrl)) {
            result.error = QStringLiteral("请输入有效的网址");
            return result;
        }
    }

    if (obj.contains(QStringLiteral("icon"))) {
        result.hasIconField = true;
        result.icon = obj.value(QStringLiteral("icon")).toString();
        if (!isValidWebsiteIcon(result.icon)) {
            result.error = QStringLiteral("图标地址无效");
            return result;
        }
    }

    if (obj.contains(QStringLiteral("has_open"))) {
        if (!obj.value(QStringLiteral("has_open")).isBool()) {
            result.error = QStringLiteral("参数无效");
            return result;
        }
        result.hasOpenField = true;
        result.hasOpen = obj.value(QStringLiteral("has_open")).toBool();
    }

    result.ok = true;
    return result;
}

bool WebsiteValidator::validateId(const QString &id)
{
    return !id.trimmed().isEmpty();
}
