#include "website/websiteiconfetcher.h"
#include "website/websiteiconstorage.h"

#include <QHostAddress>
#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include "test_util.h"

namespace {

QByteArray minimalPngBytes()
{
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::red);

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

void writeHttpResponse(QTcpSocket *socket, const QByteArray &body, const QString &contentType)
{
    QByteArray response;
    response += "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: ";
    response += contentType.toUtf8();
    response += "\r\nContent-Length: ";
    response += QByteArray::number(body.size());
    response += "\r\nConnection: close\r\n\r\n";
    response += body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

class MockIconHttpServer
{
public:
    explicit MockIconHttpServer(const QByteArray &faviconBody,
                                const QByteArray &homepageBody = {})
        : m_faviconBody(faviconBody)
        , m_homepageBody(homepageBody)
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            QTcpSocket *socket = m_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                const QByteArray request = socket->readAll();
                if (request.contains("/favicon.ico")) {
                    writeHttpResponse(socket, m_faviconBody, QStringLiteral("image/png"));
                    return;
                }
                writeHttpResponse(socket, m_homepageBody, QStringLiteral("text/html; charset=utf-8"));
            });
        });
        Q_ASSERT(m_server.listen(QHostAddress::LocalHost));
    }

    quint16 port() const
    {
        return m_server.serverPort();
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1/").arg(port());
    }

private:
    QTcpServer m_server;
    QByteArray m_faviconBody;
    QByteArray m_homepageBody;
};

bool waitForSignalCount(QSignalSpy &spy, int expectedCount, int timeoutMs = 5000)
{
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += 100) {
        if (spy.size() >= expectedCount) {
            return true;
        }
        QTest::qWait(100);
    }
    return spy.size() >= expectedCount;
}

} // namespace

class TestWebsiteIconFetcher : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void defaultFaviconUrlResolvesFromSiteUrl();
    void parseIconLinksFromHtmlFindsIconLinks();
    void parseIconLinksFromHtmlSkipsDuplicates();
    void isValidImageDataAcceptsPng();
    void isValidImageDataRejectsHtml();
    void saveDownloadedIconWritesPngFile();
    void saveDownloadedIconRejectsInvalidData();
    void fetcherDownloadsFaviconIco();
    void fetcherFallsBackToHtmlLink();

private:
    QTemporaryDir m_tempDir;
};

void TestWebsiteIconFetcher::init()
{
    QVERIFY(m_tempDir.isValid());
    WebsiteIconStorage::setIconsStorageDirectory(m_tempDir.path() + QStringLiteral("/website-icons"));
}

void TestWebsiteIconFetcher::cleanup()
{
    WebsiteIconStorage::setIconsStorageDirectory({});
}

void TestWebsiteIconFetcher::defaultFaviconUrlResolvesFromSiteUrl()
{
    const QUrl siteUrl(QStringLiteral("https://www.example.com/path/page"));
    const QUrl faviconUrl = defaultFaviconUrl(siteUrl);
    QCOMPARE(faviconUrl.scheme(), QStringLiteral("https"));
    QCOMPARE(faviconUrl.host(), QStringLiteral("www.example.com"));
    QCOMPARE(faviconUrl.path(), QStringLiteral("/favicon.ico"));
}

void TestWebsiteIconFetcher::parseIconLinksFromHtmlFindsIconLinks()
{
    const QByteArray html =
        "<!doctype html>\n"
        "<html>\n"
        "<head>\n"
        "<link rel=\"shortcut icon\" href=\"/assets/favicon.png\">\n"
        "<link rel=\"apple-touch-icon\" href=\"https://cdn.example.com/apple.png\">\n"
        "</head>\n"
        "<body></body>\n"
        "</html>";

    const QList<QUrl> urls = parseIconLinksFromHtml(html, QUrl(QStringLiteral("https://example.com")));
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls.at(0).path(), QStringLiteral("/assets/favicon.png"));
    QCOMPARE(urls.at(1).toString(), QStringLiteral("https://cdn.example.com/apple.png"));
}

void TestWebsiteIconFetcher::parseIconLinksFromHtmlSkipsDuplicates()
{
    const QByteArray html =
        "<link rel=\"icon\" href=\"/favicon.png\">\n"
        "<link rel=\"icon\" href=\"/favicon.png\">";

    const QList<QUrl> urls = parseIconLinksFromHtml(html, QUrl(QStringLiteral("https://example.com")));
    QCOMPARE(urls.size(), 1);
}

void TestWebsiteIconFetcher::isValidImageDataAcceptsPng()
{
    QVERIFY(isValidImageData(minimalPngBytes()));
}

void TestWebsiteIconFetcher::isValidImageDataRejectsHtml()
{
    QVERIFY(!isValidImageData("<html></html>"));
}

void TestWebsiteIconFetcher::saveDownloadedIconWritesPngFile()
{
    const QString fileUrl = WebsiteIconStorage::saveDownloadedIcon(
        QStringLiteral("website-id-1"), minimalPngBytes());
    QVERIFY(fileUrl.startsWith(QStringLiteral("file:")));
    QVERIFY(QFileInfo::exists(QUrl(fileUrl).toLocalFile()));
}

void TestWebsiteIconFetcher::saveDownloadedIconRejectsInvalidData()
{
    QVERIFY(WebsiteIconStorage::saveDownloadedIcon(
                QStringLiteral("website-id-2"), QByteArray("<html></html>"))
                .isEmpty());
}

void TestWebsiteIconFetcher::fetcherDownloadsFaviconIco()
{
    MockIconHttpServer server(minimalPngBytes());
    QNetworkAccessManager network;

    WebsiteIconFetcher fetcher;
    fetcher.setNetworkAccessManager(&network);

    QSignalSpy fetchedSpy(&fetcher, &WebsiteIconFetcher::iconFetched);
    QSignalSpy failedSpy(&fetcher, &WebsiteIconFetcher::fetchFailed);

    fetcher.fetchForWebsite(QStringLiteral("fetch-test-1"), server.baseUrl());
    QVERIFY(waitForSignalCount(fetchedSpy, 1));
    QCOMPARE(failedSpy.size(), 0);

    const QString websiteId = fetchedSpy.at(0).at(0).toString();
    const QString fileUrl = fetchedSpy.at(0).at(1).toString();
    QCOMPARE(websiteId, QStringLiteral("fetch-test-1"));
    QVERIFY(fileUrl.startsWith(QStringLiteral("file:")));
}

void TestWebsiteIconFetcher::fetcherFallsBackToHtmlLink()
{
    QTcpServer iconServer;
    QObject::connect(&iconServer, &QTcpServer::newConnection, &iconServer, [&iconServer]() {
        QTcpSocket *socket = iconServer.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            socket->readAll();
            writeHttpResponse(socket, minimalPngBytes(), QStringLiteral("image/png"));
        });
    });
    QVERIFY(iconServer.listen(QHostAddress::LocalHost));

    const QByteArray html = QStringLiteral(
                                "<link rel=\"icon\" href=\"http://127.0.0.1:%1/icon-from-html.png\">")
                                .arg(iconServer.serverPort())
                                .toUtf8();

    QTcpServer siteServer;
    QObject::connect(&siteServer, &QTcpServer::newConnection, &siteServer, [&siteServer, html]() {
        QTcpSocket *socket = siteServer.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, html]() {
            const QByteArray request = socket->readAll();
            if (request.contains("/favicon.ico")) {
                writeHttpResponse(socket, QByteArray("<html></html>"), QStringLiteral("text/html"));
                return;
            }
            writeHttpResponse(socket, html, QStringLiteral("text/html; charset=utf-8"));
        });
    });
    QVERIFY(siteServer.listen(QHostAddress::LocalHost));

    const QString siteUrl = QStringLiteral("http://127.0.0.1:%1/").arg(siteServer.serverPort());

    QNetworkAccessManager network;
    WebsiteIconFetcher fetcher;
    fetcher.setNetworkAccessManager(&network);

    QSignalSpy fetchedSpy(&fetcher, &WebsiteIconFetcher::iconFetched);
    fetcher.fetchForWebsite(QStringLiteral("fetch-test-2"), siteUrl);
    QVERIFY(waitForSignalCount(fetchedSpy, 1));
    QVERIFY(fetchedSpy.at(0).at(1).toString().startsWith(QStringLiteral("file:")));
}

QTEST_CORE_MAIN(TestWebsiteIconFetcher)
#include "tst_websiteiconfetcher.moc"
