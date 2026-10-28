#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

constexpr quint64 kSteamId64Base = 76561197960265728ULL;
constexpr quint8 kObject = 0;
constexpr quint8 kString = 1;
constexpr quint8 kInt32 = 2;
constexpr quint8 kUint64 = 7;
constexpr quint8 kEnd = 8;

struct VdfNode {
    quint8 type = kObject;
    QByteArray key;
    QByteArray stringValue;
    qint32 int32Value = 0;
    quint64 uint64Value = 0;
    QVector<VdfNode> children;
};

class VdfReader final
{
public:
    explicit VdfReader(const QByteArray &data) : m_data(data) {}

    bool read(QVector<VdfNode> *nodes, QString *error)
    {
        return readObject(nodes, error);
    }

private:
    bool readCString(QByteArray *value, QString *error)
    {
        const qsizetype end = m_data.indexOf('\0', m_offset);
        if (end < 0) {
            *error = QStringLiteral("unterminated binary VDF string");
            return false;
        }
        *value = m_data.sliced(m_offset, end - m_offset);
        m_offset = end + 1;
        return true;
    }

    bool readBytes(void *destination, qsizetype size, QString *error)
    {
        if (m_offset + size > m_data.size()) {
            *error = QStringLiteral("truncated binary VDF value");
            return false;
        }
        memcpy(destination, m_data.constData() + m_offset, static_cast<size_t>(size));
        m_offset += size;
        return true;
    }

    bool readObject(QVector<VdfNode> *nodes, QString *error)
    {
        while (m_offset < m_data.size()) {
            const quint8 type = static_cast<quint8>(m_data.at(m_offset++));
            if (type == kEnd) {
                return true;
            }
            if (type != kObject && type != kString && type != kInt32 && type != kUint64) {
                *error = QStringLiteral("unsupported binary VDF type %1").arg(type);
                return false;
            }

            VdfNode node;
            node.type = type;
            if (!readCString(&node.key, error)) {
                return false;
            }
            if (type == kObject) {
                if (!readObject(&node.children, error)) {
                    return false;
                }
            } else if (type == kString) {
                if (!readCString(&node.stringValue, error)) {
                    return false;
                }
            } else if (type == kInt32) {
                qint32 value = 0;
                if (!readBytes(&value, sizeof(value), error)) {
                    return false;
                }
                node.int32Value = value;
            } else {
                quint64 value = 0;
                if (!readBytes(&value, sizeof(value), error)) {
                    return false;
                }
                node.uint64Value = value;
            }
            nodes->append(std::move(node));
        }

        *error = QStringLiteral("binary VDF object is missing its terminator");
        return false;
    }

    const QByteArray &m_data;
    qsizetype m_offset = 0;
};

void appendCString(QByteArray *output, const QByteArray &value)
{
    output->append(value);
    output->append('\0');
}

void writeObject(QByteArray *output, const QVector<VdfNode> &nodes)
{
    for (const VdfNode &node : nodes) {
        output->append(static_cast<char>(node.type));
        appendCString(output, node.key);
        if (node.type == kObject) {
            writeObject(output, node.children);
        } else if (node.type == kString) {
            appendCString(output, node.stringValue);
        } else if (node.type == kInt32) {
            output->append(reinterpret_cast<const char *>(&node.int32Value), sizeof(node.int32Value));
        } else if (node.type == kUint64) {
            output->append(reinterpret_cast<const char *>(&node.uint64Value), sizeof(node.uint64Value));
        }
    }
    output->append(static_cast<char>(kEnd));
}

VdfNode stringNode(const QByteArray &key, const QByteArray &value)
{
    VdfNode node;
    node.type = kString;
    node.key = key;
    node.stringValue = value;
    return node;
}

VdfNode intNode(const QByteArray &key, qint32 value)
{
    VdfNode node;
    node.type = kInt32;
    node.key = key;
    node.int32Value = value;
    return node;
}

VdfNode objectNode(const QByteArray &key, QVector<VdfNode> children = {})
{
    VdfNode node;
    node.type = kObject;
    node.key = key;
    node.children = std::move(children);
    return node;
}

VdfNode *findNode(QVector<VdfNode> *nodes, const QByteArray &key)
{
    for (VdfNode &node : *nodes) {
        if (node.key == key) {
            return &node;
        }
    }
    return nullptr;
}

const VdfNode *findNode(const QVector<VdfNode> &nodes, const QByteArray &key)
{
    for (const VdfNode &node : nodes) {
        if (node.key == key) {
            return &node;
        }
    }
    return nullptr;
}

quint32 crc32(const QByteArray &value)
{
    quint32 crc = 0xFFFFFFFFU;
    for (const unsigned char byte : value) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

quint32 shortcutId(const QByteArray &exe, const QByteArray &name)
{
    return crc32(exe + name) | 0x80000000U;
}

QStringList steamRoots()
{
    const QString home = QDir::homePath();
    return {
        home + QStringLiteral("/.local/share/Steam"),
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/root"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
    };
}

QString canonicalPath(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
}

QStringList uniquePaths(const QStringList &paths)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString &path : paths) {
        const QString canonical = canonicalPath(path);
        if (seen.contains(canonical)) {
            continue;
        }
        seen.insert(canonical);
        result.append(canonical);
    }
    return result;
}

QString activeUserConfigDir(const QString &root)
{
    QFile file(QDir(root).filePath(QStringLiteral("config/loginusers.vdf")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QString text = QString::fromUtf8(file.readAll());
    QRegularExpression userPattern(QStringLiteral("\\\"(\\d{17})\\\"\\s*\\{([\\s\\S]*?)\\}"));
    QRegularExpressionMatchIterator matches = userPattern.globalMatch(text);
    QString fallback;
    int candidateCount = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        bool ok = false;
        const quint64 steamId64 = match.captured(1).toULongLong(&ok);
        if (!ok || steamId64 <= kSteamId64Base) {
            continue;
        }
        const quint64 accountId = steamId64 - kSteamId64Base;
        const QString configDir = QDir(root).filePath(
            QStringLiteral("userdata/%1/config").arg(accountId));
        if (!QDir(configDir).exists()) {
            continue;
        }
        if (fallback.isEmpty()) {
            fallback = configDir;
        }
        ++candidateCount;
        const QString block = match.captured(2);
        if (QRegularExpression(QStringLiteral("\\\"AutoLogin\\\"\\s+\\\"1\\\"")).match(block).hasMatch()) {
            return configDir;
        }
    }
    return candidateCount == 1 ? fallback : QString();
}

QStringList shortcutTargets(QString *error)
{
    QStringList activeTargets;
    QStringList existingTargets;
    QStringList userConfigDirs;

    for (const QString &root : uniquePaths(steamRoots())) {
        const QString userdata = QDir(root).filePath(QStringLiteral("userdata"));
        if (!QDir(userdata).exists()) {
            continue;
        }
        const QString activeConfig = activeUserConfigDir(root);
        if (!activeConfig.isEmpty()) {
            activeTargets.append(QDir(activeConfig).filePath(QStringLiteral("shortcuts.vdf")));
        }

        QDir userDir(userdata);
        const QFileInfoList users = userDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &user : users) {
            const QString config = QDir(user.absoluteFilePath()).filePath(QStringLiteral("config"));
            if (!QDir(config).exists()) {
                continue;
            }
            userConfigDirs.append(config);
            const QString shortcut = QDir(config).filePath(QStringLiteral("shortcuts.vdf"));
            if (QFileInfo::exists(shortcut)) {
                existingTargets.append(shortcut);
            }
        }
    }

    activeTargets = uniquePaths(activeTargets);
    if (!activeTargets.isEmpty()) {
        return activeTargets;
    }

    userConfigDirs = uniquePaths(userConfigDirs);
    if (userConfigDirs.size() > 1) {
        *error = QStringLiteral("Multiple Steam users were found, but the active user could not be determined.");
        return {};
    }

    existingTargets = uniquePaths(existingTargets);
    if (!existingTargets.isEmpty()) {
        return existingTargets;
    }
    if (userConfigDirs.size() == 1) {
        return {QDir(userConfigDirs.first()).filePath(QStringLiteral("shortcuts.vdf"))};
    }
    return {};
}

QStringList existingShortcutPaths()
{
    QStringList paths;
    for (const QString &root : uniquePaths(steamRoots())) {
        const QString userdata = QDir(root).filePath(QStringLiteral("userdata"));
        if (!QDir(userdata).exists()) {
            continue;
        }
        const QFileInfoList users = QDir(userdata).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &user : users) {
            const QString path = QDir(user.absoluteFilePath()).filePath(QStringLiteral("config/shortcuts.vdf"));
            if (QFileInfo::exists(path)) {
                paths.append(path);
            }
        }
    }
    return uniquePaths(paths);
}

QByteArray fieldString(const QVector<VdfNode> &nodes, const QByteArray &key)
{
    const VdfNode *node = findNode(nodes, key);
    return node && node->type == kString ? node->stringValue : QByteArray();
}

void updateShortcutEntry(VdfNode *entry,
                         const QByteArray &name,
                         const QByteArray &exe,
                         quint32 appId,
                         const QByteArray &icon,
                         const QByteArray &launchOptions)
{
    const QSet<QByteArray> managedKeys{
        "appid", "AppName", "exe", "StartDir", "icon", "ShortcutPath", "LaunchOptions",
        "IsHidden", "AllowDesktopConfig", "AllowOverlay", "OpenVR", "Devkit", "DevkitGameID",
        "DevkitOverrideAppID", "LastPlayTime", "FlatpakAppID", "tags",
    };
    entry->children.erase(std::remove_if(entry->children.begin(), entry->children.end(), [&managedKeys](const VdfNode &node) {
                              return managedKeys.contains(node.key);
                          }),
                          entry->children.end());

    const qint32 signedAppId = appId >= 0x80000000U
        ? static_cast<qint32>(static_cast<qint64>(appId) - 0x100000000LL)
        : static_cast<qint32>(appId);
    entry->children.append(intNode("appid", signedAppId));
    entry->children.append(stringNode("AppName", name));
    entry->children.append(stringNode("exe", exe));
    QByteArray launcherPath = exe;
    if (launcherPath.size() >= 2 && launcherPath.startsWith('"') && launcherPath.endsWith('"')) {
        launcherPath = launcherPath.sliced(1, launcherPath.size() - 2);
    }
    const QByteArray startDir = QFileInfo(QString::fromUtf8(launcherPath)).absolutePath().toUtf8();
    entry->children.append(stringNode("StartDir", startDir));
    entry->children.append(stringNode("icon", icon));
    entry->children.append(stringNode("ShortcutPath", startDir));
    entry->children.append(stringNode("LaunchOptions", launchOptions));
    entry->children.append(intNode("IsHidden", 0));
    entry->children.append(intNode("AllowDesktopConfig", 1));
    entry->children.append(intNode("AllowOverlay", 1));
    entry->children.append(intNode("OpenVR", 0));
    entry->children.append(intNode("Devkit", 0));
    entry->children.append(stringNode("DevkitGameID", ""));
    entry->children.append(intNode("DevkitOverrideAppID", 0));
    entry->children.append(intNode("LastPlayTime", 0));
    entry->children.append(stringNode("FlatpakAppID", ""));
    entry->children.append(objectNode("tags", {stringNode("0", "Browser")}));
}

bool copyArtwork(const QString &artworkDir, const QString &gridDir, quint32 appId, QString *error)
{
    if (!QDir().mkpath(gridDir)) {
        *error = QStringLiteral("could not create Steam artwork directory: %1").arg(gridDir);
        return false;
    }
    const QList<QPair<QString, QString>> artwork{
        {QStringLiteral("aerocore-browser-cover.png"), QStringLiteral("%1p.png").arg(appId)},
        // Steam Gaming Mode uses the logo artwork for the large selected-app
        // tile. Reuse the cover as a safe placeholder until a dedicated logo
        // artwork is available; without this file Steam renders that area
        // blank for the first selected shortcut.
        {QStringLiteral("aerocore-browser-cover.png"), QStringLiteral("%1_logo.png").arg(appId)},
        // Steam's Gaming Mode selected-app card requests the header artwork
        // using this legacy JPG filename, independently of the Hero asset.
        {QStringLiteral("aerocore-browser-header.png"), QStringLiteral("%1_header.jpg").arg(appId)},
        // The wide foreground card in the library home view is the Preview
        // artwork and uses the bare unsigned app id as its filename.
        {QStringLiteral("aerocore-browser-preview.png"), QStringLiteral("%1.png").arg(appId)},
        {QStringLiteral("aerocore-browser-hero.png"), QStringLiteral("%1_hero.png").arg(appId)},
    };
    for (const auto &[sourceName, targetName] : artwork) {
        const QString source = QDir(artworkDir).filePath(sourceName);
        if (!QFileInfo::exists(source)) {
            continue;
        }
        const QString target = QDir(gridDir).filePath(targetName);
        QFile::remove(target);
        if (!QFile::copy(source, target)) {
            *error = QStringLiteral("could not copy Steam artwork to %1").arg(target);
            return false;
        }
    }
    return true;
}

int listShortcuts(const QStringList &paths)
{
    QTextStream out(stdout);
    for (const QString &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QVector<VdfNode> root;
        QString error;
        if (!VdfReader(file.readAll()).read(&root, &error)) {
            QTextStream(stderr) << path << ": " << error << '\n';
            return 1;
        }
        out << path << '\n';
        const VdfNode *shortcuts = findNode(root, "shortcuts");
        if (!shortcuts || shortcuts->type != kObject) {
            continue;
        }
        for (const VdfNode &entry : shortcuts->children) {
            if (entry.type != kObject) {
                continue;
            }
            out << "  " << entry.key << ": " << fieldString(entry.children, "AppName")
                << " -> " << fieldString(entry.children, "exe") << '\n';
        }
    }
    return 0;
}

int installShortcut(const QString &path,
                    const QString &name,
                    const QString &launcher,
                    const QString &icon,
                    const QString &artworkDir,
                    const QString &launchOptions)
{
    const QByteArray exe = QByteArray("\"") + QFileInfo(launcher).absoluteFilePath().toUtf8() + QByteArray("\"");
    const QByteArray appName = name.toUtf8();
    QFile file(path);
    QVector<VdfNode> root;
    QString error;
    if (file.exists() && file.size() > 0) {
        if (!file.open(QIODevice::ReadOnly) || !VdfReader(file.readAll()).read(&root, &error)) {
            QTextStream(stderr) << "Could not read " << path << ": " << error << '\n';
            return 1;
        }
    }

    VdfNode *shortcuts = findNode(&root, "shortcuts");
    if (!shortcuts) {
        root.append(objectNode("shortcuts"));
        shortcuts = &root.last();
    }
    if (shortcuts->type != kObject) {
        QTextStream(stderr) << "Invalid shortcuts object in " << path << '\n';
        return 1;
    }

    VdfNode *entry = nullptr;
    for (VdfNode &candidate : shortcuts->children) {
        if (candidate.type != kObject) {
            continue;
        }
        if (fieldString(candidate.children, "AppName") == appName
            || fieldString(candidate.children, "exe") == exe) {
            entry = &candidate;
            break;
        }
    }

    QString action = QStringLiteral("added");
    quint32 appId = shortcutId(exe, appName);
    if (!entry) {
        QSet<int> used;
        for (const VdfNode &candidate : shortcuts->children) {
            bool ok = false;
            const int index = candidate.key.toInt(&ok);
            if (ok) {
                used.insert(index);
            }
        }
        int index = 0;
        while (used.contains(index)) {
            ++index;
        }
        shortcuts->children.append(objectNode(QByteArray::number(index)));
        entry = &shortcuts->children.last();
    } else {
        action = QStringLiteral("updated");
        const VdfNode *appid = findNode(entry->children, "appid");
        if (appid && appid->type == kInt32) {
            appId = static_cast<quint32>(appid->int32Value);
        }
    }

    updateShortcutEntry(entry, appName, exe, appId, icon.toUtf8(), launchOptions.toUtf8());
    QByteArray output;
    writeObject(&output, root);

    if (file.exists()) {
        QFile::remove(path + QStringLiteral(".bak"));
        if (!QFile::copy(path, path + QStringLiteral(".bak"))) {
            QTextStream(stderr) << "Could not back up " << path << '\n';
            return 1;
        }
    }
    QSaveFile save(path);
    if (!save.open(QIODevice::WriteOnly) || save.write(output) != output.size() || !save.commit()) {
        QTextStream(stderr) << "Could not write " << path << '\n';
        return 1;
    }

    QString artworkError;
    if (!copyArtwork(artworkDir, QFileInfo(path).dir().filePath(QStringLiteral("grid")), appId, &artworkError)) {
        QTextStream(stderr) << artworkError << '\n';
        return 1;
    }

    QTextStream out(stdout);
    out << action << ' ' << name << '\n'
        << "shortcuts: " << path << '\n'
        << "artwork id: " << appId << '\n'
        << "Start Steam again and open Gaming Mode to verify the entry.\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("aerocore-steam-shortcut"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Register AeroCore Browser as a Steam non-Steam app."));
    parser.addHelpOption();
    const QCommandLineOption listOption(QStringLiteral("list"), QStringLiteral("list existing Steam shortcuts"));
    const QCommandLineOption nameOption(QStringLiteral("name"), QStringLiteral("shortcut name"), QStringLiteral("name"), QStringLiteral("汽核浏览器"));
    const QCommandLineOption launcherOption(QStringLiteral("launcher"), QStringLiteral("launcher path"), QStringLiteral("path"));
    const QCommandLineOption iconOption(QStringLiteral("icon"), QStringLiteral("icon path"), QStringLiteral("path"));
    const QCommandLineOption artworkOption(QStringLiteral("artwork-dir"), QStringLiteral("Steam artwork directory"), QStringLiteral("path"));
    const QCommandLineOption launchOptionsOption(QStringLiteral("launch-options"), QStringLiteral("Steam launch options"), QStringLiteral("options"));
    const QCommandLineOption fileOption(QStringLiteral("file"), QStringLiteral("override shortcuts.vdf path"), QStringLiteral("path"));
    parser.addOption(listOption);
    parser.addOption(nameOption);
    parser.addOption(launcherOption);
    parser.addOption(iconOption);
    parser.addOption(artworkOption);
    parser.addOption(launchOptionsOption);
    parser.addOption(fileOption);
    parser.process(app);

    QStringList existing = existingShortcutPaths();
    if (parser.isSet(listOption)) {
        if (parser.isSet(fileOption)) {
            existing = {parser.value(fileOption)};
        }
        return listShortcuts(existing);
    }

    const QString launcher = parser.value(launcherOption);
    if (launcher.isEmpty() || !QFileInfo::exists(launcher)) {
        QTextStream(stderr) << "launcher does not exist: " << launcher << '\n';
        return 1;
    }

    QString targetError;
    const QStringList targets = parser.isSet(fileOption)
        ? QStringList{parser.value(fileOption)}
        : shortcutTargets(&targetError);
    if (targets.isEmpty()) {
        QTextStream(stderr) << (targetError.isEmpty()
                                  ? QStringLiteral("Steam user configuration was not found. Start Steam once, then retry.")
                                  : targetError)
                          << '\n';
        return 1;
    }

    const QString icon = parser.value(iconOption);
    const QString artworkDir = parser.value(artworkOption);
    return installShortcut(targets.first(), parser.value(nameOption), launcher, icon, artworkDir,
                           parser.value(launchOptionsOption));
}
