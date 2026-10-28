#include "website/websitevalidator.h"

#include <QTest>

class TestWebsiteValidator : public QObject
{
    Q_OBJECT

private slots:
    void addRejectsEmptyName();
    void addRejectsWhitespaceName();
    void addRejectsLongName();
    void addAcceptsMaxLengthName();
    void addRejectsEmptyUrl();
    void addAcceptsHttpsUrl();
    void addAcceptsUrlWithoutScheme();
    void addRejectsSearchStyleInput();
    void addRejectsInvalidSchemes();
    void editRejectsInvalidJson();
    void editRejectsMissingId();
    void editRejectsLongName();
    void editRejectsInvalidUrl();
    void editRejectsInvalidIcon();
    void editAcceptsPartialUpdate();
};

void TestWebsiteValidator::addRejectsEmptyName()
{
    const ValidationResult result = WebsiteValidator::validateAddParams(QString(), QStringLiteral("https://example.com"));
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("网站名称不能为空"));
}

void TestWebsiteValidator::addRejectsWhitespaceName()
{
    const ValidationResult result = WebsiteValidator::validateAddParams(QStringLiteral("   "), QStringLiteral("https://example.com"));
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("网站名称不能为空"));
}

void TestWebsiteValidator::addRejectsLongName()
{
    const QString name(21, QLatin1Char('a'));
    const ValidationResult result = WebsiteValidator::validateAddParams(name, QStringLiteral("https://example.com"));
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("网站名称不能超过20个字符"));
}

void TestWebsiteValidator::addAcceptsMaxLengthName()
{
    const QString name(20, QLatin1Char('a'));
    const ValidationResult result = WebsiteValidator::validateAddParams(name, QStringLiteral("https://example.com"));
    QVERIFY(result.ok);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.trimmedName, name);
}

void TestWebsiteValidator::addRejectsEmptyUrl()
{
    const ValidationResult result = WebsiteValidator::validateAddParams(QStringLiteral("site"), QString());
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("网站地址不能为空"));
}

void TestWebsiteValidator::addAcceptsHttpsUrl()
{
    const ValidationResult result = WebsiteValidator::validateAddParams(QStringLiteral("site"), QStringLiteral("https://example.com"));
    QVERIFY(result.ok);
    QCOMPARE(result.normalizedUrl, QStringLiteral("https://example.com"));
}

void TestWebsiteValidator::addAcceptsUrlWithoutScheme()
{
    const ValidationResult result = WebsiteValidator::validateAddParams(QStringLiteral("site"), QStringLiteral("example.com"));
    QVERIFY(result.ok);
    QCOMPARE(result.normalizedUrl, QStringLiteral("https://example.com"));
}

void TestWebsiteValidator::addRejectsSearchStyleInput()
{
    const ValidationResult hello = WebsiteValidator::validateAddParams(QStringLiteral("site"), QStringLiteral("hello"));
    QVERIFY(!hello.ok);
    QCOMPARE(hello.error, QStringLiteral("请输入有效的网址"));

    const ValidationResult search = WebsiteValidator::validateAddParams(QStringLiteral("site"), QStringLiteral("hello world"));
    QVERIFY(!search.ok);
    QCOMPARE(search.error, QStringLiteral("请输入有效的网址"));
}

void TestWebsiteValidator::addRejectsInvalidSchemes()
{
    const ValidationResult ftp = WebsiteValidator::validateAddParams(QStringLiteral("site"), QStringLiteral("ftp://example.com"));
    QVERIFY(!ftp.ok);

    const ValidationResult about = WebsiteValidator::validateAddParams(QStringLiteral("site"), QStringLiteral("about:blank"));
    QVERIFY(!about.ok);
}

void TestWebsiteValidator::editRejectsInvalidJson()
{
    QVERIFY(!WebsiteValidator::validateEditParams(QString()).ok);
    QVERIFY(!WebsiteValidator::validateEditParams(QStringLiteral("not-json")).ok);
    QVERIFY(!WebsiteValidator::validateEditParams(QStringLiteral("[]")).ok);
}

void TestWebsiteValidator::editRejectsMissingId()
{
    const ValidationResult missing = WebsiteValidator::validateEditParams(QStringLiteral("{\"name\":\"site\"}"));
    QVERIFY(!missing.ok);

    const ValidationResult emptyId = WebsiteValidator::validateEditParams(QStringLiteral("{\"id\":\"  \"}"));
    QVERIFY(!emptyId.ok);
}

void TestWebsiteValidator::editRejectsLongName()
{
    const QString json = QStringLiteral("{\"id\":\"abc\",\"name\":\"%1\"}").arg(QString(21, QLatin1Char('a')));
    QVERIFY(!WebsiteValidator::validateEditParams(json).ok);
}

void TestWebsiteValidator::editRejectsInvalidUrl()
{
    const ValidationResult result = WebsiteValidator::validateEditParams(
        QStringLiteral("{\"id\":\"abc\",\"url\":\"hello\"}"));
    QVERIFY(!result.ok);
}

void TestWebsiteValidator::editRejectsInvalidIcon()
{
    const ValidationResult result = WebsiteValidator::validateEditParams(
        QStringLiteral("{\"id\":\"abc\",\"icon\":\"bad-scheme://icon\"}"));
    QVERIFY(!result.ok);
}

void TestWebsiteValidator::editAcceptsPartialUpdate()
{
    const ValidationResult result = WebsiteValidator::validateEditParams(
        QStringLiteral("{\"id\":\"abc\",\"name\":\"new-name\"}"));
    QVERIFY(result.ok);
    QCOMPARE(result.id, QStringLiteral("abc"));
    QVERIFY(result.hasNameField);
    QCOMPARE(result.trimmedName, QStringLiteral("new-name"));
}

QTEST_APPLESS_MAIN(TestWebsiteValidator)
#include "tst_websitevalidator.moc"
