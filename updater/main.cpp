#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

#include <memory>

#ifndef AEROCORE_BROWSER_VERSION
#define AEROCORE_BROWSER_VERSION "0.1.0"
#endif

namespace {

bool isAllowedReleaseUrl(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http")
        && qEnvironmentVariableIsSet("AEROCORE_ALLOW_INSECURE_HTTP")) {
        return true;
    }
#ifdef AEROCORE_ALLOW_INSECURE_HTTP
    return scheme == QStringLiteral("https") || scheme == QStringLiteral("http");
#else
    return scheme == QStringLiteral("https");
#endif
}

}

namespace {

QString installDir()
{
    const QString configured = qEnvironmentVariable("AEROCORE_BROWSER_INSTALL_DIR");
    if (!configured.isEmpty()) {
        return QDir::cleanPath(configured);
    }
    return QDir::homePath() + QStringLiteral("/.local/share/aerocore-browser");
}

QString statePath()
{
    const QString configured = qEnvironmentVariable("AEROCORE_UPDATER_STATE");
    if (!configured.isEmpty()) {
        return configured;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/updater/state.json");
}

bool newerThan(const QString &remote, const QString &local)
{
    auto parse = [](const QString &value) {
        QStringList parts = value.section('-', 0, 0).split('.');
        QList<int> numbers;
        for (int i = 0; i < 3; ++i) {
            bool ok = false;
            const int number = i < parts.size() ? parts.at(i).toInt(&ok) : 0;
            if (!ok) {
                return QList<int>{};
            }
            numbers.append(number);
        }
        return numbers;
    };
    const QList<int> left = parse(remote);
    const QList<int> right = parse(local);
    if (left.size() != 3 || right.size() != 3) {
        return false;
    }
    for (int index = 0; index < 3; ++index) {
        if (left.at(index) != right.at(index)) {
            return left.at(index) > right.at(index);
        }
    }
    return false;
}

QString installedVersion()
{
    QFile file(installDir() + QStringLiteral("/manifest.yaml"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral(AEROCORE_BROWSER_VERSION);
    }
    const QString contents = QString::fromUtf8(file.readAll());
    const QRegularExpression expression(QStringLiteral("^version:\\s*[\\\"]?([^\\\"\\s]+)"),
                                         QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = expression.match(contents);
    return match.hasMatch() ? match.captured(1) : QStringLiteral(AEROCORE_BROWSER_VERSION);
}

QString manifestVariant(const QString &manifestPath)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QRegularExpression expression(QStringLiteral("^variant:\\s*[\\\"]?([^\\\"\\s]+)"),
                                         QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = expression.match(QString::fromUtf8(file.readAll()));
    return match.hasMatch() ? match.captured(1) : QString();
}

void saveState(const QString &phase, const QString &latest = {}, const QString &error = {})
{
    const QString path = statePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QJsonObject state{{QStringLiteral("phase"), phase},
                      {QStringLiteral("installed_version"), installedVersion()},
                      {QStringLiteral("latest_version"), latest},
                      {QStringLiteral("error"), error}};
    file.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
    file.commit();
}

QByteArray download(const QUrl &url, QString *error)
{
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(url));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        *error = reply->errorString();
        reply->deleteLater();
        return {};
    }
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    return data;
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("unable to write %1").arg(path);
        return false;
    }
    return true;
}

struct Release {
    QString version;
    QUrl archive;
    QUrl checksum;
};

bool resolveRelease(Release *result, QString *error)
{
    const QString sourcePath = qEnvironmentVariable(
        "AEROCORE_RELEASE_SOURCE", installDir() + QStringLiteral("/config/release-source.json"));
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("release source is missing: %1").arg(sourcePath);
        return false;
    }
    const QJsonObject config = QJsonDocument::fromJson(source.readAll()).object();
    const QString apiBase = config.value(QStringLiteral("api_base")).toString().trimmed();
    const QString repository = config.value(QStringLiteral("repository")).toString();
    const QString templateName = config.value(QStringLiteral("tarball_template")).toString();
    const QString suffix = config.value(QStringLiteral("checksum_suffix")).toString(QStringLiteral(".sha256"));
    const QUrl apiBaseUrl(apiBase);
    if ((!apiBaseUrl.isValid() || !isAllowedReleaseUrl(apiBaseUrl)) || repository.isEmpty()) {
        *error = QStringLiteral("invalid release source configuration");
        return false;
    }

    const QUrl latestUrl(apiBase + QStringLiteral("/repos/") + repository + QStringLiteral("/releases/latest"));
    const QByteArray body = download(latestUrl, error);
    if (body.isEmpty()) {
        return false;
    }
    const QJsonObject release = QJsonDocument::fromJson(body).object();
    if (release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool()) {
        *error = QStringLiteral("latest release is not stable");
        return false;
    }
    result->version = release.value(QStringLiteral("tag_name")).toString();
    if (result->version.startsWith('v')) {
        result->version.remove(0, 1);
    }
    QString archiveName = templateName;
    archiveName.replace(QStringLiteral("{version}"), result->version);

    const QString checksumName = archiveName + suffix;
    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &assetValue : assets) {
        const QJsonObject asset = assetValue.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (name == archiveName) result->archive = url;
        if (name == checksumName) result->checksum = url;
    }
    if (!result->archive.isValid() || !result->checksum.isValid()
        || !isAllowedReleaseUrl(result->archive)
        || !isAllowedReleaseUrl(result->checksum)) {
        *error = QStringLiteral("release assets are missing or insecure");
        return false;
    }
    return true;
}

bool safeArchive(const QString &archive, const QString &destination, QString *error)
{
    QProcess list;
    list.setProgram(QStringLiteral("tar"));
    list.setArguments({QStringLiteral("-tzf"), archive});
    list.start();
    if (!list.waitForFinished(30000) || list.exitCode() != 0) {
        *error = QStringLiteral("unable to inspect update archive");
        return false;
    }
    const QStringList entries = QString::fromLocal8Bit(list.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        if (entry.startsWith('/') || entry.contains(QStringLiteral("../")) || entry == QStringLiteral("..")) {
            *error = QStringLiteral("update archive contains an unsafe path");
            return false;
        }
    }
    QDir().mkpath(destination);
    QProcess extract;
    extract.setProgram(QStringLiteral("tar"));
    extract.setArguments({QStringLiteral("-xzf"), archive, QStringLiteral("-C"), destination});
    extract.start();
    if (!extract.waitForFinished(120000) || extract.exitCode() != 0) {
        *error = QStringLiteral("unable to extract update archive");
        return false;
    }
    return true;
}

bool applyUpdate(const Release &release, QString *error)
{
    saveState(QStringLiteral("downloading"), release.version);
    const QString target = installDir();
    const QString parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent)) {
        *error = QStringLiteral("unable to create installation directory");
        return false;
    }
    QTemporaryDir temporary(parent + QStringLiteral("/.aerocore-browser-updater-XXXXXX"));
    if (!temporary.isValid()) {
        *error = QStringLiteral("unable to create temporary directory");
        return false;
    }
    const QString archivePath = temporary.filePath(QStringLiteral("update.tar.gz"));
    const QByteArray checksum = download(release.checksum, error);
    const QByteArray archive = download(release.archive, error);
    if (archive.isEmpty() || checksum.isEmpty() || !writeFile(archivePath, archive, error)) {
        return false;
    }
    saveState(QStringLiteral("verifying"), release.version);
    const QString expected = QString::fromUtf8(checksum).trimmed().split(QRegularExpression(QStringLiteral("\\s+"))).value(0).toLower();
    const QString actual = QString::fromLatin1(QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex());
    if (expected.isEmpty() || expected != actual) {
        *error = QStringLiteral("SHA-256 checksum mismatch");
        return false;
    }
    saveState(QStringLiteral("extracting"), release.version);
    const QString extracted = temporary.filePath(QStringLiteral("payload"));
    if (!safeArchive(archivePath, extracted, error)) {
        return false;
    }
    const QStringList roots = QDir(extracted).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (roots.size() != 1) {
        *error = QStringLiteral("update archive has an invalid layout");
        return false;
    }
    const QString payload = QDir(extracted).filePath(roots.first());
    if (!QFileInfo::exists(QDir(payload).filePath(QStringLiteral("aerocore-browser")))) {
        *error = QStringLiteral("update archive does not contain the Browser executable");
        return false;
    }
    // Older installations were implicitly the Chinese distribution.
    QString currentVariant = manifestVariant(QDir(target).filePath(QStringLiteral("manifest.yaml")));
    if (currentVariant.isEmpty()) currentVariant = QStringLiteral("zh-CN");
    const QString incomingVariant = manifestVariant(QDir(payload).filePath(QStringLiteral("manifest.yaml")));
    if (incomingVariant != currentVariant) {
        *error = QStringLiteral("update archive belongs to a different distribution");
        return false;
    }
    saveState(QStringLiteral("installing"), release.version);
    const QString backup = target + QStringLiteral(".previous");
    if (QFileInfo::exists(backup)) {
        QDir(backup).removeRecursively();
    }
    if (QFileInfo::exists(target) && !QDir().rename(target, backup)) {
        *error = QStringLiteral("unable to move the current installation");
        return false;
    }
    if (!QDir().rename(payload, target)) {
        QDir().rename(backup, target);
        *error = QStringLiteral("unable to activate the new installation");
        return false;
    }
    QDir(backup).removeRecursively();
    saveState(QStringLiteral("succeeded"), release.version);
    return true;
}

int command(const QString &name)
{
    const QString current = installedVersion();
    if (name == QStringLiteral("status")) {
        QTextStream(stdout) << "installed_version=" << current << '\n';
        return 0;
    }
    Release release;
    QString error;
    if (!resolveRelease(&release, &error)) {
        saveState(QStringLiteral("failed"), {}, error);
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    if (name == QStringLiteral("check")) {
        saveState(newerThan(release.version, current) ? QStringLiteral("update_available") : QStringLiteral("idle"), release.version);
        QTextStream(stdout) << "installed_version=" << current << '\n'
                            << "latest_version=" << release.version << '\n'
                            << "update_available=" << (newerThan(release.version, current) ? "1" : "0") << '\n';
        return 0;
    }
    if (name == QStringLiteral("apply")) {
        if (!newerThan(release.version, current)) {
            QTextStream(stdout) << "Already up to date.\n";
            return 0;
        }
        if (!applyUpdate(release, &error)) {
            saveState(QStringLiteral("failed"), release.version, error);
            QTextStream(stderr) << error << '\n';
            return 1;
        }
        QTextStream(stdout) << "Updated to " << release.version << ".\n";
        return 0;
    }
    QTextStream(stderr) << "Usage: aerocore-browser-updater check|status|apply\n";
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("aerocore-browser-updater"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AEROCORE_BROWSER_VERSION));
    return command(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("status"));
}
