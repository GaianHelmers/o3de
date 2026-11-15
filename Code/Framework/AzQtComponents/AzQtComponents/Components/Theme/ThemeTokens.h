/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzQtComponents/AzQtComponentsAPI.h>
#include <AzQtComponents/Components/Theme/ThemeToken.h>

#if !defined(Q_MOC_RUN)
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVector>
#endif

namespace AzQtComponents
{
    //! The registry of the tokens (ThemeToken.h): every token by its key, the value a theme gives it, and the Themed values to
    //! build again. It only holds and writes values; ThemeManager decides which theme they come from and polishes the
    //! application after a change. The registry lives for the whole process and is used from the GUI thread; tokens may list
    //! themselves from any thread (a module's static initialization).
    class AZ_QT_COMPONENTS_API ThemeTokens
    {
    public:
        //! What the registry knows of one key
        struct Info
        {
            QByteArray m_key;           //!< "tier.FarBG"
            QString m_label;
            QString m_description;      //!< where the token shows, in a sentence; empty: its declaration says nothing
            ThemeLevel m_level = ThemeLevel::Advanced;
            ThemeKind m_kind = ThemeKind::Color;
            QByteArray m_source;        //!< the key of the token it follows; empty: it has a value of its own
            QVariant m_original;        //!< the value of O3DE Original (a follower's: its source's)
            QVariant m_value;           //!< the value in effect
            QString m_text;             //!< what the theme in effect writes for this token; empty: it sets none
        };

        //! A group of the theme editor: the tokens whose keys start with `m_key.`
        struct Group
        {
            QByteArray m_key;
            QString m_title;
            QString m_description;
        };

        // ---- Reading
        static QVector<Info> all();                         //!< every token, in the order the catalogues declare them
        static QVector<Group> groups();                     //!< in the order of their first token
        static bool contains(const QByteArray& key);
        static Info info(const QByteArray& key);            //!< a default Info with an empty key: no such token
        //! The key of the token whose value lives at this address (a token read through a reference, in any module); empty if
        //! the address is no token's. For code that is handed a value by reference and keeps it for later: by the key it reads
        //! the theme's value again (ScrollBar::setBackgroundColor does).
        static QByteArray keyOf(const void* value);
        //! Goes up whenever the tokens are written. What is kept from a token outside a Themed value (an image rendered in its
        //! colour, the tile of a grid) is as old as the generation it was made at. Read without a lock: it is asked at paint.
        static int generation();

        // ---- Setting: nothing is drawn again here (ThemeManager does that)
        //! The values of a theme, as text (toText): each key it names takes its value, every other token goes back to O3DE
        //! Original's or follows its source. A text `$key` follows the token of that key. A key no token has is kept and
        //! applies when a module that declares it loads. A text that is no value of its token's kind is not applied (the token
        //! keeps what it would have without it); those keys are returned.
        static QVector<QByteArray> setTheme(const QHash<QByteArray, QString>& values);
        static bool setValue(const QByteArray& key, const QString& text);       //!< false: the text is no value of the token's kind
        static void clearValue(const QByteArray& key);      //!< back to the source's value or O3DE Original's
        //! Builds every Themed value again, in the order they were first built
        static void rebuild();

        // ---- Values as text (theme files, the theme editor)
        static QString toText(ThemeKind kind, const QVariant& value);
        //! An invalid variant: the text is no value of this kind
        static QVariant fromText(ThemeKind kind, const QString& text);

    private:
        friend class ThemeToken;
        friend class ThemeGroup;
        friend class ThemedLink;

        static void add(ThemeToken* token);
        static void remove(ThemeToken* token);
        static void addGroup(const char* key, const char* title, const char* description);
        static void addThemed(void* themed, ThemedLink::Rebuild rebuild);
        static void removeThemed(void* themed);
        //! Writes the value in effect into every copy of every token; the caller holds the registry's lock
        static void writeAll();
    };
} // namespace AzQtComponents
