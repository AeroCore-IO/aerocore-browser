#pragma once

#include <QList>
#include <QHash>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;

QUrl defaultFaviconUrl(const QUrl &siteUrl);
QList<QUrl> parseIconLinksFromHtml(const QByteArray &html, const QUrl &baseUrl);
bool isValidImageData(const QByteArray &data);

class WebsiteIconFetcher : public QObject
{
    Q_OBJECT

public:
    explicit WebsiteIconFetcher(QObject *parent = nullptr);
    ~WebsiteIconFetcher() override;

    void setNetworkAccessManager(QNetworkAccessManager *networkAccessManager);

    void fetchForWebsite(const QString &websiteId, const QString &normalizedUrl);

signals:
    void iconFetched(const QString &websiteId, const QString &fileUrl);
    void fetchFailed(const QString &websiteId);

private:
    enum class FetchStage {
        FaviconIco,
        Homepage,
        IconUrl,
    };

    struct FetchContext {
        QString websiteId;
        QString normalizedUrl;
        FetchStage stage = FetchStage::FaviconIco;
        QList<QUrl> iconCandidates;
        int iconCandidateIndex = 0;
    };

    void cancelFetch(const QString &websiteId);
    void startRequest(FetchContext context, const QUrl &url);
    void handleReplyFinished(class QNetworkReply *reply);
    void fetchHomepage(const FetchContext &context);
    void fetchNextIconCandidate(FetchContext context);
    void finishWithIcon(const FetchContext &context, const QByteArray &data);
    void finishWithFailure(const QString &websiteId);

    QNetworkAccessManager *m_network = nullptr;
    bool m_ownsNetwork = false;
    QHash<class QNetworkReply *, FetchContext> m_activeContexts;
};
