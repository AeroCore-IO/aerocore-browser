#include "website/websiteiconfetcher.h"

#include "website/websiteiconstorage.h"

#include <QImage>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>

namespace {

constexpr int kTransferTimeoutMs = 10000;

QNetworkRequest buildRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(kTransferTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

bool relContainsIconToken(const QString &rel)
{
    const QString normalized = rel.toLower();
    return normalized.contains(QStringLiteral("icon"))
        || normalized.contains(QStringLiteral("apple-touch-icon"));
}

} // namespace

QUrl defaultFaviconUrl(const QUrl &siteUrl)
{
    return siteUrl.resolved(QUrl(QStringLiteral("/favicon.ico")));
}

QList<QUrl> parseIconLinksFromHtml(const QByteArray &html, const QUrl &baseUrl)
{
    QList<QUrl> urls;
    QSet<QString> seen;

    static const QRegularExpression linkTagPattern(
        QStringLiteral(R"(<link\b[^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression relPattern(
        QStringLiteral(R"rx(\brel\s*=\s*("([^"]*)"|'([^']*)'|([^\s>]+)))rx"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression hrefPattern(
        QStringLiteral(R"rx(\bhref\s*=\s*("([^"]*)"|'([^']*)'|([^\s>]+)))rx"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = linkTagPattern.globalMatch(QString::fromUtf8(html));
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);

        const QRegularExpressionMatch relMatch = relPattern.match(tag);
        if (!relMatch.hasMatch()) {
            continue;
        }

        const QString rel = relMatch.captured(2).isEmpty()
            ? (relMatch.captured(3).isEmpty() ? relMatch.captured(4) : relMatch.captured(3))
            : relMatch.captured(2);
        if (!relContainsIconToken(rel)) {
            continue;
        }

        const QRegularExpressionMatch hrefMatch = hrefPattern.match(tag);
        if (!hrefMatch.hasMatch()) {
            continue;
        }

        const QString href = hrefMatch.captured(2).isEmpty()
            ? (hrefMatch.captured(3).isEmpty() ? hrefMatch.captured(4) : hrefMatch.captured(3))
            : hrefMatch.captured(2);
        if (href.isEmpty()) {
            continue;
        }

        const QUrl resolved = baseUrl.resolved(QUrl(href));
        if (!resolved.isValid() || resolved.scheme().isEmpty()) {
            continue;
        }

        const QString scheme = resolved.scheme().toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")
            && scheme != QStringLiteral("data")) {
            continue;
        }

        const QString key = resolved.toString(QUrl::FullyEncoded);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        urls.append(resolved);
    }

    return urls;
}

bool isValidImageData(const QByteArray &data)
{
    if (data.isEmpty()) {
        return false;
    }

    QImage image;
    return image.loadFromData(data);
}

WebsiteIconFetcher::WebsiteIconFetcher(QObject *parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_ownsNetwork = true;
}

WebsiteIconFetcher::~WebsiteIconFetcher()
{
    for (auto it = m_activeContexts.begin(); it != m_activeContexts.end(); ++it) {
        if (it.key()) {
            it.key()->abort();
        }
    }
    m_activeContexts.clear();
}

void WebsiteIconFetcher::setNetworkAccessManager(QNetworkAccessManager *networkAccessManager)
{
    for (auto it = m_activeContexts.begin(); it != m_activeContexts.end(); ++it) {
        if (it.key()) {
            it.key()->abort();
            it.key()->deleteLater();
        }
    }
    m_activeContexts.clear();

    if (m_ownsNetwork && m_network) {
        m_network->deleteLater();
    }

    m_network = networkAccessManager;
    m_ownsNetwork = false;
}

void WebsiteIconFetcher::fetchForWebsite(const QString &websiteId, const QString &normalizedUrl)
{
    if (websiteId.isEmpty() || normalizedUrl.isEmpty() || !m_network) {
        return;
    }

    cancelFetch(websiteId);

    const QUrl siteUrl(normalizedUrl);
    if (!siteUrl.isValid()) {
        finishWithFailure(websiteId);
        return;
    }

    FetchContext context;
    context.websiteId = websiteId;
    context.normalizedUrl = normalizedUrl;
    context.stage = FetchStage::FaviconIco;
    startRequest(context, defaultFaviconUrl(siteUrl));
}

void WebsiteIconFetcher::cancelFetch(const QString &websiteId)
{
    for (auto it = m_activeContexts.begin(); it != m_activeContexts.end();) {
        if (it.value().websiteId == websiteId) {
            if (it.key()) {
                it.key()->abort();
                it.key()->deleteLater();
            }
            it = m_activeContexts.erase(it);
        } else {
            ++it;
        }
    }
}

void WebsiteIconFetcher::startRequest(FetchContext context, const QUrl &url)
{
    if (!m_network || !url.isValid()) {
        finishWithFailure(context.websiteId);
        return;
    }

    QNetworkReply *reply = m_network->get(buildRequest(url));
    m_activeContexts.insert(reply, context);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReplyFinished(reply);
    });
}

void WebsiteIconFetcher::handleReplyFinished(QNetworkReply *reply)
{
    const FetchContext context = m_activeContexts.take(reply);
    reply->deleteLater();

    const QByteArray data = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool httpOk = reply->error() == QNetworkReply::NoError
        && (statusCode == 0 || (statusCode >= 200 && statusCode < 300));

    if (context.stage == FetchStage::FaviconIco) {
        if (httpOk && isValidImageData(data)) {
            finishWithIcon(context, data);
            return;
        }
        fetchHomepage(context);
        return;
    }

    if (context.stage == FetchStage::Homepage) {
        if (!httpOk) {
            finishWithFailure(context.websiteId);
            return;
        }

        FetchContext nextContext = context;
        nextContext.stage = FetchStage::IconUrl;
        nextContext.iconCandidates = parseIconLinksFromHtml(data, QUrl(context.normalizedUrl));
        nextContext.iconCandidateIndex = 0;
        fetchNextIconCandidate(nextContext);
        return;
    }

    if (context.stage == FetchStage::IconUrl) {
        if (httpOk && isValidImageData(data)) {
            finishWithIcon(context, data);
            return;
        }

        fetchNextIconCandidate(context);
    }
}

void WebsiteIconFetcher::fetchHomepage(const FetchContext &context)
{
    FetchContext nextContext = context;
    nextContext.stage = FetchStage::Homepage;
    startRequest(nextContext, QUrl(context.normalizedUrl));
}

void WebsiteIconFetcher::fetchNextIconCandidate(FetchContext context)
{
    while (context.iconCandidateIndex < context.iconCandidates.size()) {
        const QUrl candidate = context.iconCandidates.at(context.iconCandidateIndex);
        ++context.iconCandidateIndex;

        if (!candidate.isValid()) {
            continue;
        }

        if (candidate.scheme() == QStringLiteral("data")) {
            const QString encoded = candidate.toString(QUrl::FullyEncoded);
            const int commaIndex = encoded.indexOf(QLatin1Char(','));
            if (commaIndex >= 0) {
                const QByteArray payload = QByteArray::fromBase64(
                    encoded.mid(commaIndex + 1).toUtf8());
                if (isValidImageData(payload)) {
                    finishWithIcon(context, payload);
                    return;
                }
            }
            continue;
        }

        startRequest(context, candidate);
        return;
    }

    finishWithFailure(context.websiteId);
}

void WebsiteIconFetcher::finishWithIcon(const FetchContext &context, const QByteArray &data)
{
    const QString fileUrl = WebsiteIconStorage::saveDownloadedIcon(context.websiteId, data);
    if (fileUrl.isEmpty()) {
        finishWithFailure(context.websiteId);
        return;
    }

    emit iconFetched(context.websiteId, fileUrl);
}

void WebsiteIconFetcher::finishWithFailure(const QString &websiteId)
{
    emit fetchFailed(websiteId);
}
