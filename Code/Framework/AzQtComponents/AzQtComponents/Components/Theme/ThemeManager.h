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
#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#endif

class QTimer;

namespace AzQtComponents
{
    // ----------------------------------------------------------------------------------
    // A theme as a file holds it
    // ----------------------------------------------------------------------------------

    //! One theme: the values it gives the tokens (ThemeToken.h), over the theme it is based on. A file `<name>.theme.json`:
    //!
    //!     {
    //!         "name": "Modern Blue Steel",
    //!         "description": "...",
    //!         "author": "...",
    //!         "base": "O3DE Original",
    //!         "colors": { "tier.FarBG": "#101115", "field.Line": "$tier.FarBG" },
    //!         "structure": { "field.Radius": 4, "field.Margin": "2 2 2 2", "surface.Family": "Open Sans" }
    //!     }
    //!
    //! A key is a token's key; a token the file does not name has the value of the base theme, and below every theme lie the
    //! values in the code, the O3DE Original theme. A colour is "#RRGGBB" or "#RRGGBBAA", a length a number, margins "left top
    //! right bottom", and "$key" follows the token of that key. `colors` and `structure` are one list in two parts, for the reader.
    struct AZ_QT_COMPONENTS_API ThemeFile
    {
        QString m_name;                         //!< what the theme editor shows, and what another theme's `base` names
        QString m_description;
        QString m_author;
        QString m_base;                         //!< empty: O3DE Original
        QMap<QByteArray, QString> m_values;     //!< key -> the value as text
        QString m_path;                         //!< the file it was read from; empty: none
        bool m_builtIn = false;                 //!< ships with the engine: never written to

        //! False, with a sentence in `error`: the file is no theme
        static bool read(const QString& path, ThemeFile& theme, QString* error = nullptr);
        bool write(const QString& path, QString* error = nullptr) const;
    };

    // ----------------------------------------------------------------------------------
    // The themes of this application
    // ----------------------------------------------------------------------------------

    //! Knows the themes (the ones the engine ships, the user's own), which one is in effect, and puts one into effect: the tokens
    //! take its values, what is built from them is built again, and the application is polished again. StyleManager::initialize
    //! makes it, in the flattened GUI only: no theme reaches the legacy GUI, and instance() is null there.
    //!
    //! An application starts with O3DE Original. One that offers themes calls restoreSavedTheme() after it initialized its
    //! StyleManager; a test never does, and so always runs O3DE Original. The environment variable O3DE_THEME names the theme of
    //! one run (a capture, a benchmark) and is never saved.
    //!
    //! The engine's themes are never written to: the first edit of one makes a theme of the user's that is based on it, and the
    //! user's themes save themselves as they are edited.
    class AZ_QT_COMPONENTS_API ThemeManager
        : public QObject
    {
        Q_OBJECT

    public:
        //! The theme of the values in the code. It has no file.
        static constexpr const char* OriginalName = "O3DE Original";
        //! The environment variable that names the theme of this run
        static constexpr const char* ThemeVariable = "O3DE_THEME";
        //! The environment variable that names the folder of the user's themes for this run (a benchmark edits themes, and
        //! must leave the user's own alone)
        static constexpr const char* UserFolderVariable = "O3DE_THEMES_FOLDER";
        //! With O3DE_THEME: the milliseconds after which that theme takes effect. Unset, it does before the first widget exists;
        //! set, the application starts as O3DE Original and changes while it runs, which is what a test of the change needs.
        static constexpr const char* ThemeDelayVariable = "O3DE_THEME_DELAY_MS";

        //! Null in the legacy GUI, and before StyleManager::initialize
        static ThemeManager* instance();

        //! Puts the theme the user chose last into effect (the one O3DE_THEME names, if it is set). For an application to call
        //! once, after StyleManager::initialize. Nothing happens in the legacy GUI.
        static void restoreSavedTheme();

        //! `builtInFolder`: the engine's themes on disk (a source build); empty or missing: the ones in the resources.
        //! `userFolder`: the user's themes and the saved choice; empty: `Themes` below the O3DE manifest folder (~/.o3de).
        ThemeManager(const QString& builtInFolder, const QString& userFolder, QObject* parent);
        ~ThemeManager() override;

        // ---- The themes
        QStringList themes() const;                         //!< O3DE Original, the engine's, then the user's, each by name
        bool contains(const QString& name) const;
        bool isBuiltIn(const QString& name) const;          //!< O3DE Original and the engine's: they cannot be changed or removed
        ThemeFile theme(const QString& name) const;         //!< an empty theme with no name: no such theme
        QString activeTheme() const;
        QString userFolder() const;

        //! What the last change of theme or of a value cost, by part
        struct ApplyTiming
        {
            qint64 m_tokensUs = 0;      //!< microseconds: the values written into every copy of every token
            qint64 m_rebuildUs = 0;     //!< the Themed values built again
            qint64 m_polishUs = 0;      //!< the application polished again (Style::applyTheme)
            int m_widgets = 0;          //!< the widgets the application had
        };
        ApplyTiming lastApplyTiming() const;
        //! Reads the folders again: a theme file was added or changed outside the application
        void reload();

        // ---- Choosing
        //! Puts the theme into effect and saves the choice (not when O3DE_THEME named the theme of this run). False: no such theme.
        bool setActiveTheme(const QString& name);

        // ---- Editing the theme in effect
        //! Gives a token a value. Applies at once and is saved a moment later. The first edit of one of the engine's themes makes
        //! a theme of the user's based on it and puts that into effect. False: the text is no value of the token's kind.
        bool setValue(const QByteArray& key, const QString& text);
        //! Gives several tokens values in one change of theme (a scheme made from two colours), as setValue does one. The
        //! keys whose text is no value of their token's kind are left out, and returned.
        QVector<QByteArray> setValues(const QHash<QByteArray, QString>& values);
        //! Shows the theme in effect with these values over it and changes no theme: what a dialog does while it is open.
        //! endPreview() shows the theme in effect again; setValue and setValues end a preview too.
        void preview(const QHash<QByteArray, QString>& values);
        void endPreview();
        //! Takes the theme's own value of a token away: it has the base theme's again
        void resetValue(const QByteArray& key);
        //! Every value of the theme in effect, back to its base theme's
        void resetAllValues();
        //! True if the theme in effect gives the token a value of its own (not through its base)
        bool hasOwnValue(const QByteArray& key) const;

        // ---- The user's themes
        //! A theme of the user's that reads as `name` does: one that lies over an engine theme (and follows it), or a copy of one
        //! of the user's. The new theme's name (a name taken gets a number), empty on failure.
        QString duplicate(const QString& name, const QString& newName);
        bool rename(const QString& name, const QString& newName);
        //! The description and the author of the theme in effect. False: it is one of the engine's.
        bool setDetails(const QString& description, const QString& author);
        bool remove(const QString& name);                   //!< the theme in effect: its base theme takes over
        //! Copies a theme file into the user's themes; the name it has there (a name taken gets a number), empty on failure
        QString importTheme(const QString& path, QString* error = nullptr);
        //! Writes a theme with every value it has in effect, so that it reads the same wherever its base theme is missing
        bool exportTheme(const QString& name, const QString& path, QString* error = nullptr) const;

    Q_SIGNALS:
        void themeApplied();        //!< the tokens changed and the application was polished again
        void themesChanged();       //!< the list of themes, or the theme in effect, changed

    private:
        void readThemes();
        //! Every value of the theme and of the themes below it, the theme's own last
        QHash<QByteArray, QString> valuesOf(const QString& name) const;
        //! Tokens, Themed values, then the application's polish. The keys of the theme whose text their token does not read.
        QVector<QByteArray> apply();
        //! The Themed values and the application's polish, after the tokens were written (which took tokensUs)
        void rebuildAndPolish(qint64 tokensUs);
        //! The theme of the user's that edits go to: the theme in effect, or a new one based on it
        ThemeFile* editable();
        ThemeFile* find(const QString& name);
        const ThemeFile* find(const QString& name) const;
        QString freeName(const QString& wanted) const;
        QString pathFor(const QString& name) const;
        void saveSoon();
        void saveNow();
        void saveChoice() const;

        QString m_builtInFolder;
        QString m_userFolder;
        QVector<ThemeFile> m_themes;        //!< the engine's first, then the user's
        QString m_active;
        bool m_choiceIsForThisRunOnly = false;      //!< O3DE_THEME named the theme: the choice is not saved
        bool m_unsaved = false;
        bool m_previewing = false;                  //!< the tokens hold a preview's values over the theme in effect
        ApplyTiming m_lastApply;
        QTimer* m_saveTimer = nullptr;
    };
} // namespace AzQtComponents
