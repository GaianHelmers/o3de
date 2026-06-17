/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "WelcomeNewsFeed.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

#include <AzCore/Debug/Trace.h>

namespace O3DEWelcome
{
    //////////////////////////////////////////////////////////////////////////
    // Configuration + small text helpers
    //////////////////////////////////////////////////////////////////////////

    namespace
    {
        const char* FeedUrl = "https://o3de.org/feed/";
        // The server closes the connection for requests without a User-Agent, so one is mandatory.
        const char* UserAgent = "O3DE-Editor-WelcomeScreen/1.0";
        const int MaxArticles = 9;
        const int SummaryLimit = 150;

        //! Strip HTML tags and the common WordPress entities, then collapse whitespace and truncate.
        QString CleanSummary(QString text)
        {
            text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
            text.replace(QStringLiteral("&#8230;"), QStringLiteral("..."));
            text.replace(QStringLiteral("&#8217;"), QStringLiteral("'"));
            text.replace(QStringLiteral("&#8216;"), QStringLiteral("'"));
            text.replace(QStringLiteral("&#8220;"), QStringLiteral("\""));
            text.replace(QStringLiteral("&#8221;"), QStringLiteral("\""));
            text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
            text = text.simplified();

            // Drop the WordPress "The post ... appeared first on ..." trailer.
            const int trailer = text.indexOf(QStringLiteral("The post "));
            if (trailer >= 0)
            {
                text = text.left(trailer).trimmed();
            }
            if (text.length() > SummaryLimit)
            {
                text = text.left(SummaryLimit).trimmed() + QStringLiteral("...");
            }
            return text;
        }

        //! RFC822 pubDate ("Fri, 06 Mar 2026 16:02:53 +0000") -> "Mar 6, 2026".
        QString FormatDate(const QString& pubDate)
        {
            const QDateTime dateTime = QDateTime::fromString(pubDate, Qt::RFC2822Date);
            return dateTime.isValid() ? dateTime.toString(QStringLiteral("MMM d, yyyy")) : QString();
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Lifecycle
    //////////////////////////////////////////////////////////////////////////

    WelcomeNewsFeed::WelcomeNewsFeed(QObject* parent)
        : QObject(parent)
        , m_network(new QNetworkAccessManager(this))
    {
        LoadCache();
    }

    WelcomeNewsFeed::~WelcomeNewsFeed() = default;

    //////////////////////////////////////////////////////////////////////////
    // Network
    //////////////////////////////////////////////////////////////////////////

    void WelcomeNewsFeed::Refresh()
    {
        SetState(State::Loading);

        QNetworkRequest request{ QUrl(QString::fromLatin1(FeedUrl)) };
        request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(UserAgent));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply* reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]{ OnReplyFinished(reply); });
    }

    void WelcomeNewsFeed::OnReplyFinished(QNetworkReply* reply)
    {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            AZ_Warning("WelcomeNewsFeed", false, "News fetch failed (HTTP %d): %s",
                httpStatus, reply->errorString().toUtf8().constData());
            // Keep whatever we already have; the state tells the UI whether it is a fallback.
            SetState(HasArticles() ? State::Cached : State::Offline);
            return;
        }

        const QByteArray body = reply->readAll();
        const QVector<NewsArticle> parsed = ParseRss(body);
        if (parsed.isEmpty())
        {
            AZ_Warning("WelcomeNewsFeed", false,
                "News fetch returned %lld bytes but parsed zero articles.", static_cast<long long>(body.size()));
            SetState(HasArticles() ? State::Cached : State::Offline);
            return;
        }

        AZ_Printf("WelcomeNewsFeed", "Fetched %d news articles from o3de.org.\n", static_cast<int>(parsed.size()));
        m_articles = parsed;
        SaveCache();
        SetState(State::Online);
    }

    //////////////////////////////////////////////////////////////////////////
    // RSS parsing
    //////////////////////////////////////////////////////////////////////////

    QVector<NewsArticle> WelcomeNewsFeed::ParseRss(const QByteArray& xml)
    {
        QVector<NewsArticle> result;
        QXmlStreamReader reader(xml);
        NewsArticle current;
        bool inItem = false;

        while (!reader.atEnd())
        {
            reader.readNext();

            if (reader.isStartElement())
            {
                const QString name = reader.name().toString();   // namespace-aware: local name only
                if (name == QLatin1String("item"))
                {
                    inItem = true;
                    current = NewsArticle();
                }
                else if (inItem && name == QLatin1String("title"))
                {
                    current.m_title = reader.readElementText().trimmed();
                }
                else if (inItem && name == QLatin1String("link"))
                {
                    current.m_url = reader.readElementText().trimmed();
                }
                else if (inItem && name == QLatin1String("creator"))   // dc:creator
                {
                    current.m_author = reader.readElementText().trimmed();
                }
                else if (inItem && name == QLatin1String("pubDate"))
                {
                    current.m_dateText = FormatDate(reader.readElementText().trimmed());
                }
                else if (inItem && current.m_category.isEmpty() && name == QLatin1String("category"))
                {
                    current.m_category = reader.readElementText().trimmed();
                }
                else if (inItem && current.m_summary.isEmpty() && name == QLatin1String("description"))
                {
                    current.m_summary = CleanSummary(reader.readElementText());
                }
            }
            else if (reader.isEndElement() && reader.name() == QLatin1String("item"))
            {
                inItem = false;
                if (!current.m_title.isEmpty() && !current.m_url.isEmpty())
                {
                    result.push_back(current);
                    if (result.size() >= MaxArticles)
                    {
                        break;
                    }
                }
            }
        }
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Disk cache
    //////////////////////////////////////////////////////////////////////////

    QString WelcomeNewsFeed::CacheFilePath()
    {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (dir.isEmpty())
        {
            dir = QDir::tempPath();
        }
        return dir + QStringLiteral("/o3de_welcome_news.json");
    }

    void WelcomeNewsFeed::SaveCache() const
    {
        QJsonArray array;
        for (const NewsArticle& article : m_articles)
        {
            QJsonObject object;
            object[QStringLiteral("title")] = article.m_title;
            object[QStringLiteral("url")] = article.m_url;
            object[QStringLiteral("category")] = article.m_category;
            object[QStringLiteral("author")] = article.m_author;
            object[QStringLiteral("date")] = article.m_dateText;
            object[QStringLiteral("summary")] = article.m_summary;
            array.append(object);
        }

        QJsonObject root;
        root[QStringLiteral("fetched")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        root[QStringLiteral("articles")] = array;

        const QString path = CacheFilePath();
        QDir().mkpath(QFileInfo(path).absolutePath());

        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        }
    }

    void WelcomeNewsFeed::LoadCache()
    {
        QFile file(CacheFilePath());
        if (!file.open(QIODevice::ReadOnly))
        {
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
        {
            return;
        }

        QVector<NewsArticle> loaded;
        const QJsonArray array = document.object().value(QStringLiteral("articles")).toArray();
        for (const QJsonValue& value : array)
        {
            const QJsonObject object = value.toObject();
            NewsArticle article;
            article.m_title = object.value(QStringLiteral("title")).toString();
            article.m_url = object.value(QStringLiteral("url")).toString();
            article.m_category = object.value(QStringLiteral("category")).toString();
            article.m_author = object.value(QStringLiteral("author")).toString();
            article.m_dateText = object.value(QStringLiteral("date")).toString();
            article.m_summary = object.value(QStringLiteral("summary")).toString();
            if (!article.m_title.isEmpty() && !article.m_url.isEmpty())
            {
                loaded.push_back(article);
            }
        }

        if (!loaded.isEmpty())
        {
            m_articles = loaded;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // State
    //////////////////////////////////////////////////////////////////////////

    void WelcomeNewsFeed::SetState(State state)
    {
        m_state = state;
        emit Updated();
    }
}
