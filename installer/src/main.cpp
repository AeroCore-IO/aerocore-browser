#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <clocale>
#include <libintl.h>

#ifndef AEROCORE_BROWSER_VERSION
#define AEROCORE_BROWSER_VERSION "0.1.0"
#endif

namespace {

constexpr char kTextDomain[] = "aerocore-browser-installer";

QString text(const char *message)
{
    return QString::fromUtf8(gettext(message));
}

void initializeTranslations()
{
    // An unset or unsupported locale leaves gettext returning the English
    // msgid, which is the installer's intentional fallback language.
    std::setlocale(LC_MESSAGES, "");

    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList localeDirectories{
        qEnvironmentVariable("AEROCORE_INSTALLER_LOCALE_DIR"),
        QDir(applicationDir).filePath("../share/locale"),
        QDir(applicationDir).filePath("../../share/locale"),
        QDir(applicationDir).filePath("locale"),
    };

    for (const QString &directory : localeDirectories) {
        if (directory.isEmpty() || !QDir(directory).exists()) {
            continue;
        }
        const QByteArray encodedDirectory = QFile::encodeName(QDir(directory).absolutePath());
        bindtextdomain(kTextDomain, encodedDirectory.constData());
        break;
    }
    textdomain(kTextDomain);
}

QString appImageDir()
{
    const QString configured = qEnvironmentVariable("AEROCORE_APPIMAGE_DIR");
    if (!configured.isEmpty()) {
        return QDir(configured).absolutePath();
    }

    // When launched directly from an AppDir, the installer is under
    // AppDir/usr/bin.
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../..");
}

class InstallerWindow final : public QWidget
{
public:
    InstallerWindow()
    {
        setWindowTitle(text("Install AeroCore Browser"));
        setFixedSize(560, 380);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(28, 28, 28, 28);
        layout->setSpacing(14);

        auto *title = new QLabel(text("AeroCore Browser"), this);
        QFont titleFont = title->font();
        titleFont.setPointSize(titleFont.pointSize() + 6);
        titleFont.setBold(true);
        title->setFont(titleFont);

        m_status = new QLabel(text("Ready to install"), this);
        QFont statusFont = m_status->font();
        statusFont.setBold(true);
        m_status->setFont(statusFont);

        m_details = new QLabel(
            text("Install the browser for the current user. Existing files will be updated."),
            this);
        m_details->setWordWrap(true);
        m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        m_progress = new QProgressBar(this);
        m_progress->setRange(0, 0);
        m_progress->setVisible(false);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(10);
        actions->addStretch();

        m_cancel = new QPushButton(text("Cancel"), this);
        m_install = new QPushButton(text("Install"), this);
        m_uninstall = new QPushButton(text("Uninstall"), this);
        m_install->setDefault(true);
        m_install->setAutoDefault(true);
        actions->addWidget(m_cancel);
        actions->addWidget(m_uninstall);
        actions->addWidget(m_install);

        layout->addWidget(title);
        layout->addWidget(m_status);
        layout->addWidget(m_details);
        layout->addWidget(m_progress);
        layout->addLayout(actions);

        connect(m_cancel, &QPushButton::clicked, this, &QWidget::close);
        connect(m_uninstall, &QPushButton::clicked, this, [this] { startOperation(true); });
        connect(m_install, &QPushButton::clicked, this, [this] {
            if (m_installComplete) {
                close();
                return;
            }
            startOperation(false);
        });

        refreshInstallState();
    }

private:
    QString payloadPath() const
    {
        return QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("aerocore-browser-install-payload"));
    }

    void refreshInstallState()
    {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("AEROCORE_APPIMAGE_DIR"), appImageDir());
        environment.insert(QStringLiteral("AEROCORE_BROWSER_VERSION"), QStringLiteral(AEROCORE_BROWSER_VERSION));
        process.setProcessEnvironment(environment);
        const QStringList statusArguments{payloadPath(), QStringLiteral("--status")};
        process.start(QStringLiteral("bash"), statusArguments);
        if (!process.waitForFinished(2000)) {
            return;
        }

        const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
        const bool installed = output.contains(QStringLiteral("installed=1"));
        const QString installedVersion = output.section(QStringLiteral("version="), 1, 1)
                                             .section('\n', 0, 0).trimmed();
        const QString action = output.section(QStringLiteral("action="), 1, 1)
                                  .section('\n', 0, 0).trimmed();
        if (!installed) {
            m_details->setText(text("Install the browser for the current user."));
            m_uninstall->setEnabled(false);
        } else {
            m_details->setText(text("Installed version: %1\nAvailable version: %2")
                                   .arg(installedVersion, QStringLiteral(AEROCORE_BROWSER_VERSION)));
            m_uninstall->setEnabled(true);
            if (action == QStringLiteral("blocked")) {
                m_install->setText(text("Downgrade unavailable"));
                m_install->setEnabled(false);
            } else if (action == QStringLiteral("reinstall")) {
                m_install->setText(text("Reinstall"));
            } else {
                m_install->setText(text("Upgrade"));
            }
        }
    }

    void startOperation(bool uninstall)
    {
        if (m_process) {
            return;
        }

        if (!uninstall) {
            const auto choice = QMessageBox::warning(
                this,
                text("Close Steam before installation"),
                text(
                    "AeroCore Browser will be registered in Steam as a non-Steam app.\n\n"
                    "The installer must close Steam and any Steam helpers before updating "
                    "Steam's shortcut database. Continue?"),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Yes);
            if (choice != QMessageBox::Yes) {
                return;
            }
        }

        m_status->setText(uninstall ? text("Uninstalling…") : text("Installing…"));
        m_details->setText(uninstall ? text("Removing the installed application.")
                                     : text("Copying the application bundle. This may take a few minutes."));
        m_progress->setVisible(true);
        m_cancel->setEnabled(false);
        m_uninstall->setEnabled(false);
        m_install->setEnabled(false);

        m_process = new QProcess(this);
        const QString payload = payloadPath();
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("AEROCORE_APPIMAGE_DIR"), appImageDir());
        environment.insert(QStringLiteral("AEROCORE_BROWSER_VERSION"), QStringLiteral(AEROCORE_BROWSER_VERSION));
        m_process->setProcessEnvironment(environment);
        m_process->setProcessChannelMode(QProcess::MergedChannels);

        connect(m_process, &QProcess::finished, this,
                [this, uninstall](int exitCode, QProcess::ExitStatus exitStatus) {
                    const QString output = QString::fromLocal8Bit(m_process->readAll()).trimmed();
                    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
                    m_process->deleteLater();
                    m_process = nullptr;

                    if (success) {
                        m_status->setText(uninstall ? text("Uninstallation complete")
                                                     : text("Installation complete"));
                        m_details->setText(output.isEmpty()
                                               ? text("AeroCore Browser is ready from the application menu.")
                                               : output);
                        m_progress->setRange(0, 1);
                        m_progress->setValue(1);
                        m_install->setText(text("Done"));
                        m_installComplete = true;
                        m_install->setEnabled(true);
                        m_cancel->setEnabled(false);
                        m_uninstall->setEnabled(false);
                    } else {
                        m_status->setText(text("Installation failed"));
                        m_details->setText(output.isEmpty() ? text("The installer process failed.") : output);
                        m_progress->setVisible(false);
                        m_install->setText(text("Retry"));
                        m_install->setEnabled(true);
                        m_cancel->setEnabled(true);
                        m_uninstall->setEnabled(!uninstall);
                    }
                });

        QStringList arguments{payload};
        if (uninstall) {
            arguments.append(QStringLiteral("--uninstall"));
        }
        m_process->start(QStringLiteral("bash"), arguments);
        if (!m_process->waitForStarted(1000)) {
            m_details->setText(text("Unable to start the installer process."));
            m_progress->setVisible(false);
            m_install->setText(text("Retry"));
            m_install->setEnabled(true);
            m_cancel->setEnabled(true);
            m_uninstall->setEnabled(!uninstall);
            m_process->deleteLater();
            m_process = nullptr;
        }
    }

    QLabel *m_status = nullptr;
    QLabel *m_details = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_uninstall = nullptr;
    QPushButton *m_install = nullptr;
    bool m_installComplete = false;
    QProcess *m_process = nullptr;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    initializeTranslations();
    InstallerWindow window;
    window.show();
    return application.exec();
}
