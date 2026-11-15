/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzQtComponents/Components/Theme/ThemeTokens.h>

#include <QColor>
#include <QDebug>
#include <QMargins>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>

#include <atomic>

namespace AzQtComponents
{
    //! Private to this file: named, not anonymous, as a unity build merges the anonymous namespaces of its files
    namespace ThemeTokensInternal
    {
        // ------------------------------------------------------------------------------
        // The registry
        // ------------------------------------------------------------------------------

        //! One key: what its first token declared, and every copy of the token
        struct Entry
        {
            ThemeTokens::Info m_info;
            QVariant m_own;                     //!< O3DE Original's value of a token with a value of its own
            QVector<ThemeToken*> m_copies;      //!< one per module that declares the token
        };

        struct ThemedValue
        {
            void* m_themed;
            ThemedLink::Rebuild m_rebuild;
        };

        struct Registry
        {
            QMutex m_mutex;
            QVector<Entry> m_entries;                   //!< in the order of the first copy of each key
            QHash<QByteArray, int> m_index;
            QVector<ThemeTokens::Group> m_groups;
            QHash<QByteArray, QString> m_theme;         //!< the theme in effect as text, the keys no token has yet among them
            QVector<ThemedValue> m_themed;
            QSet<QByteArray> m_texts;                   //!< every text a theme has set: a ThemeText points into one
            std::atomic<int> m_generation{ 0 };         //!< the number of times the tokens were written
        };

        //! Never destroyed: the tokens of a module that unloads when the process ends still take themselves off the list
        Registry& registry()
        {
            static Registry* instance = new Registry;
            return *instance;
        }

        constexpr QChar FollowMark = QLatin1Char('$');  //!< a theme's text `$key` follows the token of that key
        constexpr int ChainLimit = 16;                  //!< no chain of followers is this long: a ring ends here

        // ------------------------------------------------------------------------------
        // Values
        // ------------------------------------------------------------------------------

        //! The value a copy holds now
        QVariant read(ThemeKind kind, const void* value)
        {
            switch (kind)
            {
            case ThemeKind::Color:
                return QVariant::fromValue(*static_cast<const QColor*>(value));
            case ThemeKind::Int:
                return QVariant(*static_cast<const int*>(value));
            case ThemeKind::Margins:
                return QVariant::fromValue(*static_cast<const QMargins*>(value));
            case ThemeKind::Text:
                return QVariant(QByteArray(*static_cast<const char* const*>(value)));
            }
            return QVariant();
        }

        void write(Registry& all, ThemeKind kind, void* value, const QVariant& from)
        {
            switch (kind)
            {
            case ThemeKind::Color:
                *static_cast<QColor*>(value) = from.value<QColor>();
                break;
            case ThemeKind::Int:
                *static_cast<int*>(value) = from.toInt();
                break;
            case ThemeKind::Margins:
                *static_cast<QMargins*>(value) = from.value<QMargins>();
                break;
            case ThemeKind::Text:
                // The set keeps the bytes for good: a value built before the next theme may still point at them
                *static_cast<const char**>(value) = all.m_texts.insert(from.toByteArray())->constData();
                break;
            }
        }

        //! O3DE Original's value of an entry: its own, or its source's
        QVariant original(const Registry& all, const Entry& entry)
        {
            const Entry* at = &entry;
            // A source is declared before its follower, so the chain ends; the limit guards a catalogue that breaks that
            for (int steps = 0; steps < ChainLimit && !at->m_info.m_source.isEmpty(); ++steps)
            {
                const auto found = all.m_index.constFind(at->m_info.m_source);
                if (found == all.m_index.constEnd())
                {
                    break;
                }
                at = &all.m_entries[found.value()];
            }
            return at->m_own;
        }

        //! The value in effect: the theme's, else the source's value in effect, else O3DE Original's. A theme's `$key` is the
        //! value in effect of that key's token, where it is of the same kind.
        QVariant current(const Registry& all, const Entry& entry)
        {
            const ThemeKind kind = entry.m_info.m_kind;
            const Entry* at = &entry;
            for (int steps = 0; steps < ChainLimit; ++steps)
            {
                QByteArray next = at->m_info.m_source;
                const auto set = all.m_theme.constFind(at->m_info.m_key);
                if (set != all.m_theme.constEnd())
                {
                    if (set.value().startsWith(FollowMark))
                    {
                        next = set.value().mid(1).toUtf8();
                    }
                    else
                    {
                        const QVariant value = ThemeTokens::fromText(kind, set.value());
                        if (value.isValid())
                        {
                            return value;
                        }
                    }
                }
                const auto found = all.m_index.constFind(next);
                if (next.isEmpty() || found == all.m_index.constEnd() || all.m_entries[found.value()].m_info.m_kind != kind)
                {
                    break;
                }
                at = &all.m_entries[found.value()];
            }
            return original(all, *at);
        }

        //! True if the theme's text for the entry is one its kind reads, or names a token of its kind
        bool applies(const Registry& all, const Entry& entry, const QString& text)
        {
            if (text.startsWith(FollowMark))
            {
                const auto found = all.m_index.constFind(text.mid(1).toUtf8());
                return found != all.m_index.constEnd() && all.m_entries[found.value()].m_info.m_kind == entry.m_info.m_kind;
            }
            return ThemeTokens::fromText(entry.m_info.m_kind, text).isValid();
        }
    } // namespace ThemeTokensInternal

    using namespace ThemeTokensInternal;

    // ----------------------------------------------------------------------------------
    // Tokens and Themed values list themselves
    // ----------------------------------------------------------------------------------

    ThemeToken::ThemeToken(ThemeKind kind, void* value, const char* key, ThemeLevel level, const char* label, const ThemeToken* source,
        const char* description)
        : m_kind(kind)
        , m_level(level)
        , m_value(value)
        , m_key(key)
        , m_label(label)
        , m_description(description ? description : "")
        , m_source(source)
    {
    }

    ThemeToken::~ThemeToken()
    {
        ThemeTokens::remove(this);
    }

    void ThemeToken::list()
    {
        ThemeTokens::add(this);
    }

    ThemeGroup::ThemeGroup(const char* key, const char* title, const char* description)
    {
        ThemeTokens::addGroup(key, title, description);
    }

    ThemedLink::ThemedLink(void* themed, Rebuild rebuild)
        : m_themed(themed)
    {
        ThemeTokens::addThemed(themed, rebuild);
    }

    ThemedLink::~ThemedLink()
    {
        ThemeTokens::removeThemed(m_themed);
    }

    void ThemeTokens::add(ThemeToken* token)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);

        const QByteArray key(token->m_key);
        auto found = all.m_index.find(key);
        if (found == all.m_index.end())
        {
            // The first copy of the key declares it: its value now is O3DE Original's (a follower holds its source's value, which
            // a theme may have changed already: its own is never read)
            Entry entry;
            entry.m_info.m_key = key;
            entry.m_info.m_label = QString::fromUtf8(token->m_label);
            entry.m_info.m_description = QString::fromUtf8(token->m_description);
            entry.m_info.m_level = token->m_level;
            entry.m_info.m_kind = token->m_kind;
            entry.m_info.m_source = token->m_source ? QByteArray(token->m_source->m_key) : QByteArray();
            entry.m_own = read(token->m_kind, token->m_value);
            all.m_entries.append(entry);
            found = all.m_index.insert(key, static_cast<int>(all.m_entries.size()) - 1);
        }
        Entry& entry = all.m_entries[found.value()];
        if (entry.m_info.m_kind != token->m_kind)
        {
            qWarning() << "ThemeTokens: the token" << key << "is declared with two kinds; the second declaration is not themed";
            return;
        }
        entry.m_copies.append(token);

        // A module that loads while a theme is in effect: its copy takes the theme's value before anything of the module reads it
        const QVariant value = current(all, entry);
        const QVariant first = original(all, entry);
        entry.m_info.m_original = first;
        entry.m_info.m_value = value;
        entry.m_info.m_text = all.m_theme.value(key);
        token->m_changed = value != first;
        if (value != read(token->m_kind, token->m_value))
        {
            write(all, token->m_kind, token->m_value, value);
        }
    }

    void ThemeTokens::remove(ThemeToken* token)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        const auto found = all.m_index.constFind(QByteArray(token->m_key));
        if (found != all.m_index.constEnd())
        {
            all.m_entries[found.value()].m_copies.removeAll(token);
        }
    }

    void ThemeTokens::addGroup(const char* key, const char* title, const char* description)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        for (const Group& group : all.m_groups)
        {
            if (group.m_key == key)
            {
                return;     // another module's copy of the same declaration
            }
        }
        all.m_groups.append(Group{ QByteArray(key), QString::fromUtf8(title), QString::fromUtf8(description) });
    }

    void ThemeTokens::addThemed(void* themed, ThemedLink::Rebuild rebuild)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        all.m_themed.append(ThemedValue{ themed, rebuild });
    }

    void ThemeTokens::removeThemed(void* themed)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        for (int at = static_cast<int>(all.m_themed.size()) - 1; at >= 0; --at)
        {
            if (all.m_themed[at].m_themed == themed)
            {
                all.m_themed.removeAt(at);
                return;
            }
        }
    }

    // ----------------------------------------------------------------------------------
    // Reading
    // ----------------------------------------------------------------------------------

    QVector<ThemeTokens::Info> ThemeTokens::all()
    {
        Registry& registered = registry();
        QMutexLocker lock(&registered.m_mutex);
        QVector<Info> infos;
        infos.reserve(registered.m_entries.size());
        for (const Entry& entry : registered.m_entries)
        {
            infos.append(entry.m_info);
        }
        return infos;
    }

    QVector<ThemeTokens::Group> ThemeTokens::groups()
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        QVector<Group> groups;
        // In the order of each group's first token; a group nobody describes shows under its key
        for (const Entry& entry : all.m_entries)
        {
            const QByteArray key = entry.m_info.m_key.left(entry.m_info.m_key.indexOf('.'));
            bool listed = false;
            for (const Group& group : groups)
            {
                listed = listed || group.m_key == key;
            }
            if (listed)
            {
                continue;
            }
            Group group{ key, QString::fromUtf8(key), QString() };
            for (const Group& described : all.m_groups)
            {
                if (described.m_key == key)
                {
                    group = described;
                }
            }
            groups.append(group);
        }
        return groups;
    }

    bool ThemeTokens::contains(const QByteArray& key)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        return all.m_index.contains(key);
    }

    ThemeTokens::Info ThemeTokens::info(const QByteArray& key)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        const auto found = all.m_index.constFind(key);
        return found == all.m_index.constEnd() ? Info() : all.m_entries[found.value()].m_info;
    }

    QByteArray ThemeTokens::keyOf(const void* value)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        for (const Entry& entry : all.m_entries)
        {
            for (const ThemeToken* copy : entry.m_copies)
            {
                if (copy->m_value == value)
                {
                    return entry.m_info.m_key;
                }
            }
        }
        return QByteArray();
    }

    // ----------------------------------------------------------------------------------
    // Setting
    // ----------------------------------------------------------------------------------

    int ThemeTokens::generation()
    {
        return registry().m_generation.load(std::memory_order_relaxed);
    }

    void ThemeTokens::writeAll()
    {
        Registry& all = registry();
        ++all.m_generation;
        for (Entry& entry : all.m_entries)
        {
            const QVariant value = current(all, entry);
            const QVariant first = original(all, entry);
            entry.m_info.m_original = first;
            entry.m_info.m_value = value;
            entry.m_info.m_text = all.m_theme.value(entry.m_info.m_key);
            for (ThemeToken* copy : entry.m_copies)
            {
                copy->m_changed = value != first;
                write(all, copy->m_kind, copy->m_value, value);
            }
        }
    }

    QVector<QByteArray> ThemeTokens::setTheme(const QHash<QByteArray, QString>& values)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        all.m_theme = values;
        QVector<QByteArray> refused;
        for (auto value = values.constBegin(); value != values.constEnd(); ++value)
        {
            const auto found = all.m_index.constFind(value.key());
            if (found != all.m_index.constEnd() && !applies(all, all.m_entries[found.value()], value.value()))
            {
                refused.append(value.key());
            }
        }
        writeAll();
        return refused;
    }

    bool ThemeTokens::setValue(const QByteArray& key, const QString& text)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        const auto found = all.m_index.constFind(key);
        if (found != all.m_index.constEnd() && !applies(all, all.m_entries[found.value()], text))
        {
            return false;
        }
        all.m_theme.insert(key, text);
        writeAll();
        return true;
    }

    void ThemeTokens::clearValue(const QByteArray& key)
    {
        Registry& all = registry();
        QMutexLocker lock(&all.m_mutex);
        if (all.m_theme.remove(key))
        {
            writeAll();
        }
    }

    void ThemeTokens::rebuild()
    {
        Registry& all = registry();
        // A copy of the list: a builder runs code of its module, which must be free to list a value of its own
        QVector<ThemedValue> themed;
        {
            QMutexLocker lock(&all.m_mutex);
            themed = all.m_themed;
        }
        for (const ThemedValue& value : themed)
        {
            value.m_rebuild(value.m_themed);
        }
    }

    // ----------------------------------------------------------------------------------
    // Values as text
    // ----------------------------------------------------------------------------------

    QString ThemeTokens::toText(ThemeKind kind, const QVariant& value)
    {
        switch (kind)
        {
        case ThemeKind::Color:
            {
                // #RRGGBB, and #RRGGBBAA where the colour is translucent (the order of CSS, not Qt's #AARRGGBB)
                const QColor color = value.value<QColor>();
                QString text = color.name(QColor::HexRgb).toUpper();
                if (color.alpha() != 255)
                {
                    text += QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0')).toUpper();
                }
                return text;
            }
        case ThemeKind::Int:
            return QString::number(value.toInt());
        case ThemeKind::Margins:
            {
                const QMargins margins = value.value<QMargins>();
                return QStringLiteral("%1 %2 %3 %4").arg(margins.left()).arg(margins.top()).arg(margins.right()).arg(margins.bottom());
            }
        case ThemeKind::Text:
            return QString::fromUtf8(value.toByteArray());
        }
        return QString();
    }

    QVariant ThemeTokens::fromText(ThemeKind kind, const QString& text)
    {
        const QString trimmed = text.trimmed();
        switch (kind)
        {
        case ThemeKind::Color:
            {
                // #RGB, #RRGGBB or #RRGGBBAA
                const qsizetype length = trimmed.size();
                if (!trimmed.startsWith(QLatin1Char('#')) || (length != 4 && length != 7 && length != 9))
                {
                    return QVariant();
                }
                QColor color = QColor::fromString(trimmed.left(length == 9 ? 7 : length));
                bool ok = color.isValid();
                if (ok && length == 9)
                {
                    color.setAlpha(trimmed.mid(7, 2).toInt(&ok, 16));
                }
                return ok ? QVariant::fromValue(color) : QVariant();
            }
        case ThemeKind::Int:
            {
                bool ok = false;
                const int number = trimmed.toInt(&ok);
                return ok ? QVariant(number) : QVariant();
            }
        case ThemeKind::Margins:
            {
                // Four lengths: left, top, right, bottom; one length: every side
                const QStringList parts = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() != 4 && parts.size() != 1)
                {
                    return QVariant();
                }
                int sides[4] = { 0, 0, 0, 0 };
                for (int side = 0; side < 4; ++side)
                {
                    bool ok = false;
                    sides[side] = parts[parts.size() == 1 ? 0 : side].toInt(&ok);
                    if (!ok)
                    {
                        return QVariant();
                    }
                }
                return QVariant::fromValue(QMargins(sides[0], sides[1], sides[2], sides[3]));
            }
        case ThemeKind::Text:
            return trimmed.isEmpty() ? QVariant() : QVariant(trimmed.toUtf8());
        }
        return QVariant();
    }
} // namespace AzQtComponents
