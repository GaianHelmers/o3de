/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzQtComponents/Utilities/IconUtilities.h>
#include <AzQtComponents/Components/Theme/ThemeTokens.h>

#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>

namespace AzQtComponents
{
    QIcon CachedIcon(const QString& filePath, const QSize& size, const QString& disabledFilePath)
    {
        // The icons built so far, by their files and size
        static QHash<QString, QIcon> s_icons;
        const QString key = (size.isValid() || !disabledFilePath.isEmpty())
            ? QStringLiteral("%1|%2x%3|%4").arg(filePath).arg(size.width()).arg(size.height()).arg(disabledFilePath)
            : filePath;
        if (const auto found = s_icons.constFind(key); found != s_icons.constEnd())
        {
            return found.value();
        }

        // As QIcon(filePath) builds it, and the disabled image after
        QIcon icon;
        icon.addFile(filePath, size, QIcon::Normal);
        if (!disabledFilePath.isEmpty())
        {
            icon.addFile(disabledFilePath, size, QIcon::Disabled);
        }
        if (!icon.isNull())
        {
            s_icons.insert(key, icon);
        }
        return icon;
    }

    //! Private to this file: named, not anonymous, as a unity build merges the anonymous namespaces of its files
    namespace IconUtilitiesInternal
    {
        //! The engine of a ThemedIcon: the source icon as it is, or in a token's colour once a theme changed the token
        class ThemedIconEngine
            : public QIconEngine
        {
        public:
            ThemedIconEngine(const QIcon& source, const QByteArray& tintKey, const QByteArray& disabledTintKey)
                : m_source(source)
                , m_tintKey(tintKey)
                , m_disabledTintKey(disabledTintKey)
            {
            }

            void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
            {
                if (!tinted())
                {
                    m_source.paint(painter, rect, Qt::AlignCenter, mode, state);
                    return;
                }
                const qreal scale = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
                painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
            }

            QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
            {
                return scaledPixmap(size, mode, state, 1.0);
            }

            QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
            {
                QPixmap pixmap = m_source.pixmap(size, scale, mode, state);
                if (tinted() && !pixmap.isNull())
                {
                    // The token's colour in place of every colour of the image, each pixel as opaque as it was
                    QPainter painter(&pixmap);
                    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    painter.fillRect(pixmap.rect(), mode == QIcon::Disabled ? m_disabledTint : m_tint);
                }
                return pixmap;
            }

            QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override
            {
                return m_source.actualSize(size, mode, state);
            }

            QIconEngine* clone() const override
            {
                return new ThemedIconEngine(m_source, m_tintKey, m_disabledTintKey);
            }

            bool isNull() override
            {
                return m_source.isNull();
            }

        private:
            //! True once a theme changed the tint; the tokens are read again after each write of the registry
            bool tinted()
            {
                const int generation = ThemeTokens::generation();
                if (generation != m_generation)
                {
                    m_generation = generation;
                    const ThemeTokens::Info tint = ThemeTokens::info(m_tintKey);
                    m_tinted = tint.m_value != tint.m_original;
                    m_tint = tint.m_value.value<QColor>();
                    m_disabledTint = ThemeTokens::info(m_disabledTintKey).m_value.value<QColor>();
                }
                return m_tinted;
            }

            QIcon m_source;
            QByteArray m_tintKey;
            QByteArray m_disabledTintKey;
            int m_generation = -1;
            bool m_tinted = false;
            QColor m_tint;
            QColor m_disabledTint;
        };

        //! What one ThemedIcon is made from. The keys of the tokens are compared as pointers: a module asks with its own
        //! copies of the tokens, whose keys are literals of that module, so two modules may hold an icon each of one image.
        struct ThemedIconKey
        {
            QString m_filePath;
            QString m_disabledFilePath;
            QSize m_size;
            const char* m_tintKey = nullptr;
            const char* m_disabledTintKey = nullptr;

            bool operator==(const ThemedIconKey& other) const
            {
                return m_tintKey == other.m_tintKey && m_disabledTintKey == other.m_disabledTintKey && m_size == other.m_size
                    && m_filePath == other.m_filePath && m_disabledFilePath == other.m_disabledFilePath;
            }
        };

        size_t qHash(const ThemedIconKey& key, size_t seed = 0)
        {
            return qHashMulti(seed, key.m_filePath, key.m_disabledFilePath, key.m_size.width(), key.m_size.height(),
                quintptr(key.m_tintKey), quintptr(key.m_disabledTintKey));
        }
    } // namespace IconUtilitiesInternal

    QIcon ThemedIcon(const QString& filePath, const ThemeToken& tint, const ThemeToken& disabledTint, const QSize& size,
        const QString& disabledFilePath)
    {
        // The icons built so far, by their files, size and tokens. Asked at every paint of an arrow: the key is no text to build
        static QHash<IconUtilitiesInternal::ThemedIconKey, QIcon> s_icons;
        const IconUtilitiesInternal::ThemedIconKey key{ filePath, disabledFilePath, size, tint.key(), disabledTint.key() };
        if (const auto found = s_icons.constFind(key); found != s_icons.constEnd())
        {
            return found.value();
        }
        const QIcon source = CachedIcon(filePath, size, disabledFilePath);
        if (source.isNull())
        {
            return source;
        }
        const QIcon icon(new IconUtilitiesInternal::ThemedIconEngine(source, tint.key(), disabledTint.key()));
        s_icons.insert(key, icon);
        return icon;
    }
} // namespace AzQtComponents
