/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace O3DEWelcome
{
    //! One parsed entry from the O3DE news feed.
    struct NewsArticle
    {
        QString m_title;
        QString m_url;
        QString m_category;
        QString m_author;
        QString m_dateText;   // pre-formatted for display, e.g. "Mar 6, 2026"
        QString m_summary;    // HTML-stripped, truncated
    };

    //! Fetches the O3DE news RSS feed (o3de.org/feed/), parses it with QXmlStreamReader, and caches the
    //! result to disk. The welcome screen shows the cache immediately and degrades gracefully when the
    //! network is unavailable. All work is asynchronous; listen to Updated().
    class WelcomeNewsFeed
        : public QObject
    {
        Q_OBJECT
    public:
        enum class State
        {
            Idle,       // nothing requested yet
            Loading,    // a network request is in flight
            Online,     // fresh articles arrived from the network
            Cached,     // network failed, but cached articles are available
            Offline     // network failed and there is no cache
        };

        explicit WelcomeNewsFeed(QObject* parent = nullptr);
        ~WelcomeNewsFeed() override;

        void Refresh();   // start an asynchronous fetch

        const QVector<NewsArticle>& Articles() const { return m_articles; }
        bool HasArticles() const { return !m_articles.isEmpty(); }
        State GetState() const { return m_state; }

    signals:
        void Updated();   // articles and/or state changed

    private:
        void LoadCache();
        void SaveCache() const;
        void SetState(State state);
        void OnReplyFinished(QNetworkReply* reply);

        static QVector<NewsArticle> ParseRss(const QByteArray& xml);
        static QString CacheFilePath();

        QNetworkAccessManager* m_network = nullptr;
        QVector<NewsArticle> m_articles;
        State m_state = State::Idle;
    };
}
