#include "history/historythumbnailstorage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "test_util.h"

class TestHistoryThumbnailStorage : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void saveReturnsValidFileUrl();
    void saveOverwritesSamePathOnRevisit();
    void removeThumbnailDeletesFile();
    void clearAllRemovesFiles();

private:
    QTemporaryDir m_tempDir;
};

void TestHistoryThumbnailStorage::init()
{
    QVERIFY(m_tempDir.isValid());
    HistoryThumbnailStorage::setThumbnailsStorageDirectory(
        m_tempDir.path() + QStringLiteral("/history-thumbnails"));
}

void TestHistoryThumbnailStorage::cleanup()
{
    HistoryThumbnailStorage::setThumbnailsStorageDirectory({});
}

void TestHistoryThumbnailStorage::saveReturnsValidFileUrl()
{
    QImage image(320, 240, QImage::Format_ARGB32);
    image.fill(Qt::red);

    const QString fileUrl = HistoryThumbnailStorage::saveThumbnail(QStringLiteral("history-1"), image);
    QVERIFY(!fileUrl.isEmpty());
    QVERIFY(fileUrl.startsWith(QStringLiteral("file:")));

    const QString localPath = QUrl(fileUrl).toLocalFile();
    QVERIFY(QFile::exists(localPath));
    QCOMPARE(QFileInfo(localPath).fileName(), QStringLiteral("history-1.png"));
}

void TestHistoryThumbnailStorage::saveOverwritesSamePathOnRevisit()
{
    QImage first(800, 600, QImage::Format_ARGB32);
    first.fill(Qt::red);
    const QString firstUrl = HistoryThumbnailStorage::saveThumbnail(QStringLiteral("history-2"), first);
    QVERIFY(!firstUrl.isEmpty());

    QImage second(200, 100, QImage::Format_ARGB32);
    second.fill(Qt::blue);
    const QString secondUrl = HistoryThumbnailStorage::saveThumbnail(QStringLiteral("history-2"), second);
    QCOMPARE(secondUrl, firstUrl);

    const QImage loaded(QUrl(secondUrl).toLocalFile());
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.pixelColor(10, 10), QColor(Qt::blue));
}

void TestHistoryThumbnailStorage::removeThumbnailDeletesFile()
{
    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::green);
    const QString fileUrl = HistoryThumbnailStorage::saveThumbnail(QStringLiteral("history-3"), image);
    QVERIFY(!fileUrl.isEmpty());

    QVERIFY(HistoryThumbnailStorage::removeThumbnail(QStringLiteral("history-3")));
    QVERIFY(!QFile::exists(QUrl(fileUrl).toLocalFile()));
}

void TestHistoryThumbnailStorage::clearAllRemovesFiles()
{
    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::yellow);
    QVERIFY(!HistoryThumbnailStorage::saveThumbnail(QStringLiteral("history-4"), image).isEmpty());
    QVERIFY(!HistoryThumbnailStorage::saveThumbnail(QStringLiteral("history-5"), image).isEmpty());

    QVERIFY(HistoryThumbnailStorage::clearAll());
    const QDir dir(HistoryThumbnailStorage::thumbnailsStorageDirectory());
    QCOMPARE(dir.entryList(QDir::Files).size(), 0);
}

QTEST_CORE_MAIN(TestHistoryThumbnailStorage)
#include "tst_historythumbnailstorage.moc"
