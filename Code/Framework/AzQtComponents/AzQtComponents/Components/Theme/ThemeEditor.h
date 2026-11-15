/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzQtComponents/AzQtComponentsAPI.h>
#include <AzQtComponents/Components/Theme/ThemeTokens.h>

#if !defined(Q_MOC_RUN)
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>
#endif

class QComboBox;
class QFontComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QSpinBox;
class QTabBar;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace AzQtComponents
{
    //! The theme editor: picks the theme in effect and edits it while the application shows the result.
    //!
    //! From the top: the theme (a picker, a button that recolours the whole theme from two colours, and a menu to duplicate,
    //! rename, describe, delete, import and export one); what is edited (Colors or Structure, how far: Basic, Standard,
    //! Advanced, and whether only what the theme changes); a search; then the tokens in their groups, each with its value and a
    //! button that takes the theme's own value away again.
    //!
    //! The rows are made from the registry (ThemeTokens): a catalogue that declares a token gives the editor a row, and nothing
    //! here names a token. Every edit goes through ThemeManager, which applies it at once and saves the theme a moment later. An
    //! application shows the widget where it likes: the Editor as a tool pane, the Gallery as a window.
    class AZ_QT_COMPONENTS_API ThemeEditor
        : public QWidget
    {
        Q_OBJECT

    public:
        explicit ThemeEditor(QWidget* parent = nullptr);
        ~ThemeEditor() override;

        //! The name an application lists the editor under
        static constexpr const char* Title = "Theme Editor";

        //! Shows the editor in a window of its own (one per application: a second call raises it), for an application that
        //! has no pane to put it in
        static ThemeEditor* showWindow(QWidget* parent);

        //! One colour of a theme carried over to a scheme of another background and accent (Recolor):
        //!   - black, white and anything close stay as they are (text, the tool tip);
        //!   - a colour of the accent's hue turns with the accent, and takes its change of saturation and lightness;
        //!   - a grey, tinted or not, takes the new background's tint and its change of lightness;
        //!   - any other colour (an error red, a success green) stays.
        //! The old background comes out as the new one, and the old accent as the new one.
        static QColor recolored(const QColor& color, const QColor& background, const QColor& newBackground, const QColor& accent,
            const QColor& newAccent);

        //! Every colour of `tokens` (the registry's ThemeTokens::all() at some moment) that has a value of its own, carried over
        //! (recolored): key -> value as a theme writes it, for the colours that change. The tokens that follow another are left
        //! to follow it.
        static QHash<QByteArray, QString> recoloredTheme(const QVector<ThemeTokens::Info>& tokens, const QColor& newBackground,
            const QColor& newAccent);

        //! The tokens Recolor reads the scheme of the theme in effect from
        static constexpr const char* BackgroundKey = "tier.Mid";
        static constexpr const char* AccentKey = "accent.Primary";

    private:
        // ---- The part of the registry shown
        enum class Part
        {
            Colors,
            Structure,
        };

        //! One token's row: its label, the editors of its kind, what it follows, and the reset button
        struct Row
        {
            QByteArray m_key;
            ThemeKind m_kind = ThemeKind::Color;
            QLabel* m_label = nullptr;
            QToolButton* m_swatch = nullptr;        //!< a colour: opens the colour picker
            QLineEdit* m_text = nullptr;            //!< a colour as #RRGGBB
            QFontComboBox* m_font = nullptr;        //!< a text: a font family
            QSpinBox* m_numbers[4] = {};            //!< a length (the first), or margins: left, top, right, bottom
            QLabel* m_follows = nullptr;
            QToolButton* m_reset = nullptr;
        };

        // ---- Building
        void buildHeader(QVBoxLayout* layout);
        void buildFilters(QVBoxLayout* layout);
        void buildMenu();
        //! The groups and rows of the part, level, filter and search chosen
        void rebuildRows();
        void addGroup(QVBoxLayout* layout, const ThemeTokens::Group& group, const QVector<ThemeTokens::Info>& tokens);
        void addRow(QGridLayout* grid, int line, const ThemeTokens::Info& token);
        bool shows(const ThemeTokens::Info& token) const;

        // ---- Showing what is in effect
        void refreshThemes();
        void refreshValues();
        void refreshRow(Row& row, const ThemeTokens::Info& token);
        void refreshStatus();

        // ---- Editing
        //! Shows the value over the theme a moment later, changing no theme: a drag in the colour picker asks many times a
        //! second
        void previewSoon(const QByteArray& key, const QString& text);
        void previewPending();
        void pickColor(const QByteArray& key);
        void rowEdited(const QByteArray& key);
        void recolorTheme();

        // ---- The menu
        void duplicateTheme();
        void renameTheme();
        void describeTheme();
        void deleteTheme();
        void importTheme();
        void exportTheme();
        void openUserFolder();

        QComboBox* m_themes = nullptr;
        QToolButton* m_recolorButton = nullptr;
        QToolButton* m_menuButton = nullptr;
        QLabel* m_description = nullptr;
        QTabBar* m_parts = nullptr;
        QComboBox* m_level = nullptr;
        QToolButton* m_changedOnly = nullptr;
        QLineEdit* m_search = nullptr;
        QScrollArea* m_scroll = nullptr;
        QLabel* m_status = nullptr;

        QVector<Row> m_rows;
        QSet<QByteArray> m_collapsed;           //!< the groups the user folded
        QTimer* m_previewTimer = nullptr;
        QByteArray m_previewKey;                //!< what the colour picker of a row passes through while it is open
        QString m_previewText;
        QString m_themeStatus;                  //!< what the status line says of the theme in effect
        bool m_refreshing = false;              //!< the editors are being set from the registry: their signals are no edits
    };
} // namespace AzQtComponents
