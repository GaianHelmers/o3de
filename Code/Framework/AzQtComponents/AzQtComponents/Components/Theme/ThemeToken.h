/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzQtComponents/AzQtComponentsAPI.h>

#if !defined(Q_MOC_RUN)
#include <QColor>
#include <QMargins>
#include <type_traits>
#endif

namespace AzQtComponents
{
    //! The values a theme sets, and what is built from them.
    //!
    //! A token is a value of the flattened GUI with a name: a colour, a length, margins or a text (a font family). It is declared
    //! once, in a catalogue (Foundations.h is the root one; a module may declare its own), with the value of the O3DE Original
    //! theme. A theme gives tokens other values while the application runs (ThemeTokens, ThemeManager).
    //!
    //! Three ways to read a token, and no other:
    //!   - Code that runs when a widget is polished or painted reads the token itself: it has the value of the theme in effect.
    //!   - A family's name for a token is a reference: `const QColor& ToolBarFill = Foundations::Mid;`
    //!   - A value with static storage that is built from tokens (an element, an edge, a brush) is Themed:
    //!     `Themed<BoxElement> ToolBar = [] { return BoxElement{ ... }; };` It is built again whenever the theme changes.
    //! A plain copy with static storage (`const QColor Fill = Foundations::Mid;`) keeps the value it was built with: a theme does
    //! not reach it. Tools/ThemeProof/theme_statics.py lists such copies.

    // ----------------------------------------------------------------------------------
    // Tokens
    // ----------------------------------------------------------------------------------

    //! How far into the theme editor a token shows. A user opts into each level: the few values a colour scheme starts from, the
    //! roles the families share, then the values single families and tools have of their own.
    enum class ThemeLevel
    {
        Basic,
        Standard,
        Advanced,
    };

    //! The kind of value a token holds; a theme file and the theme editor read and write each in its own form
    enum class ThemeKind
    {
        Color,      //!< "#RRGGBB", or "#RRGGBBAA" where it is translucent
        Int,        //!< a length in px, a font size or a font weight
        Margins,    //!< left, top, right, bottom in px
        Text,       //!< a font family
    };

    //! What every token has: its key in a theme file, how the theme editor shows it, and the token it follows, if any. A token
    //! lists itself with ThemeTokens when it is built. On Windows each module has a copy of its own of a token declared `inline`
    //! in a header: every copy lists itself under the one key, and a theme's value is written into each.
    class AZ_QT_COMPONENTS_API ThemeToken
    {
    public:
        ThemeToken(const ThemeToken&) = delete;
        ThemeToken& operator=(const ThemeToken&) = delete;

        ThemeKind kind() const { return m_kind; }
        const char* key() const { return m_key; }           //!< "tier.FarBG": the path of the token below its catalogue
        const char* label() const { return m_label; }       //!< the name the theme editor shows
        ThemeLevel level() const { return m_level; }
        //! Where the token shows, in a sentence, for the theme editor's tool tip; empty: it says nothing
        const char* description() const { return m_description; }
        //! The token this one follows while no theme gives it a value of its own; null: it has a value of its own
        const ThemeToken* source() const { return m_source; }
        //! True while a theme gives the token another value than O3DE Original's. A hook that cannot be shown to draw what the
        //! unthemed code drew (a tint over an image) is skipped while this is false.
        bool changed() const { return m_changed; }

    protected:
        ThemeToken(ThemeKind kind, void* value, const char* key, ThemeLevel level, const char* label, const ThemeToken* source,
            const char* description);
        ~ThemeToken();

        //! Called by the token's constructor once its value is set: the registry reads the value as O3DE Original's
        void list();

    private:
        friend class ThemeTokens;

        ThemeKind m_kind;
        ThemeLevel m_level;
        bool m_changed = false;
        void* m_value;                      //!< the QColor, int, QMargins or const char* the registry writes
        const char* m_key;
        const char* m_label;
        const char* m_description;
        const ThemeToken* m_source;
    };

    //! A colour token. It is a QColor: it is read wherever a QColor is.
    class ThemeColor
        : public QColor
        , public ThemeToken
    {
    public:
        //! A token with a value of its own
        ThemeColor(const QColor& original, const char* key, ThemeLevel level, const char* label, const char* description = "")
            : QColor(original)
            , ThemeToken(ThemeKind::Color, static_cast<QColor*>(this), key, level, label, nullptr, description)
        {
            list();
        }

        //! A token that follows another until a theme gives it a value of its own
        ThemeColor(const ThemeColor& source, const char* key, ThemeLevel level, const char* label, const char* description = "")
            : QColor(source)
            , ThemeToken(ThemeKind::Color, static_cast<QColor*>(this), key, level, label, &source, description)
        {
            list();
        }
    };

    //! A length, size or weight token. It reads as an int; `const int& Name = token;` is a reference to it.
    class ThemeInt
        : public ThemeToken
    {
    public:
        ThemeInt(int original, const char* key, ThemeLevel level, const char* label, const char* description = "")
            : ThemeToken(ThemeKind::Int, &m_value, key, level, label, nullptr, description)
            , m_value(original)
        {
            list();
        }

        ThemeInt(const ThemeInt& source, const char* key, ThemeLevel level, const char* label, const char* description = "")
            : ThemeToken(ThemeKind::Int, &m_value, key, level, label, &source, description)
            , m_value(source.m_value)
        {
            list();
        }

        operator const int&() const { return m_value; }

    private:
        int m_value;
    };

    //! A margins token. It is a QMargins.
    class ThemeMargins
        : public QMargins
        , public ThemeToken
    {
    public:
        ThemeMargins(const QMargins& original, const char* key, ThemeLevel level, const char* label, const char* description = "")
            : QMargins(original)
            , ThemeToken(ThemeKind::Margins, static_cast<QMargins*>(this), key, level, label, nullptr, description)
        {
            list();
        }

        ThemeMargins(const ThemeMargins& source, const char* key, ThemeLevel level, const char* label, const char* description = "")
            : QMargins(source)
            , ThemeToken(ThemeKind::Margins, static_cast<QMargins*>(this), key, level, label, &source, description)
        {
            list();
        }
    };

    //! A text token (a font family). It reads as a C string, which stays valid for the life of the application: the registry
    //! keeps every text a theme has set.
    class ThemeText
        : public ThemeToken
    {
    public:
        ThemeText(const char* original, const char* key, ThemeLevel level, const char* label, const char* description = "")
            : ThemeToken(ThemeKind::Text, &m_value, key, level, label, nullptr, description)
            , m_value(original)
        {
            list();
        }

        ThemeText(const ThemeText& source, const char* key, ThemeLevel level, const char* label, const char* description = "")
            : ThemeToken(ThemeKind::Text, &m_value, key, level, label, &source, description)
            , m_value(source.m_value)
        {
            list();
        }

        operator const char*() const { return m_value; }
        const char* text() const { return m_value; }

    private:
        const char* m_value;
    };

    //! The title a group of tokens has in the theme editor. A token belongs to the group its key starts with ("tier.FarBG" is
    //! of "tier"); a group nobody describes shows under its key. Declared beside the tokens it describes.
    class AZ_QT_COMPONENTS_API ThemeGroup
    {
    public:
        ThemeGroup(const char* key, const char* title, const char* description);
    };

    // ----------------------------------------------------------------------------------
    // Themed values
    // ----------------------------------------------------------------------------------

    //! Lists one Themed value with the registry, and takes it off the list when the value goes (a module unloads)
    class AZ_QT_COMPONENTS_API ThemedLink
    {
    public:
        using Rebuild = void (*)(void* themed);

        ThemedLink(void* themed, Rebuild rebuild);
        ~ThemedLink();
        ThemedLink(const ThemedLink&) = delete;
        ThemedLink& operator=(const ThemedLink&) = delete;

    private:
        void* m_themed;
    };

    //! A value with static storage that is built from tokens. `build` makes it when the program starts and again whenever the
    //! theme changes, in the order the Themed values were first built, so one may read another declared before it. The value
    //! stays at its address: a registry or a widget keeps its pointer to it. A Themed class type is that type (its members and
    //! its address are read as before); a Themed int, bool or pointer reads as one.
    //!
    //!     Themed<BoxElement> ToolBar = [] { return BoxElement{ .m_idle = { .m_fill = QBrush(Foundations::Mid) } }; };
    //!
    //! Not declared const: the theme builds it again.
    template<typename T, bool IsClass = std::is_class_v<T>>
    class Themed;

    template<typename T>
    class Themed<T, true>
        : public T
    {
    public:
        template<typename Build>
        Themed(Build build)
            : T(build())
            , m_build(build)
            , m_link(this, &Themed::rebuild)
        {
        }

        Themed(const Themed&) = delete;
        Themed& operator=(const Themed&) = delete;

    private:
        static void rebuild(void* themed)
        {
            Themed* self = static_cast<Themed*>(themed);
            static_cast<T&>(*self) = self->m_build();
        }

        T (*m_build)();
        ThemedLink m_link;
    };

    template<typename T>
    class Themed<T, false>
    {
    public:
        template<typename Build>
        Themed(Build build)
            : m_value(build())
            , m_build(build)
            , m_link(this, &Themed::rebuild)
        {
        }

        Themed(const Themed&) = delete;
        Themed& operator=(const Themed&) = delete;

        operator const T&() const { return m_value; }

    private:
        static void rebuild(void* themed)
        {
            Themed* self = static_cast<Themed*>(themed);
            self->m_value = self->m_build();
        }

        T m_value;
        T (*m_build)();
        ThemedLink m_link;
    };
} // namespace AzQtComponents
