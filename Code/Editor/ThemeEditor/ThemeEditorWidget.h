/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QSpinBox;
class QTabWidget;
class QTimer;
class QVBoxLayout;

//////////////////////////////////////////////////////////////////////////
// ThemeEditorWidget
// Editable dockable view of the active theme's token tree.
// Features:
//   - Resolves theme_base inheritance to show the FULL effective token set.
//   - Responsive collapsible group CARDS distributed into 1/2/3 columns by width.
//   - Color swatch buttons (inline-styled) that open QColorDialog.
//   - On-demand apply: "Apply to Editor" button or opt-in "Live preview" checkbox.
//   - "Save As New Theme..." persists m_effectiveFlat as a new theme folder.
//////////////////////////////////////////////////////////////////////////
class ThemeEditorWidget
    : public QWidget
{
    Q_OBJECT

public:
    explicit ThemeEditorWidget(QWidget* parent = nullptr);
    ~ThemeEditorWidget() override;

    static void RegisterViewClass();

    static constexpr const char* WidgetName = "Theme Editor";

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void RebuildFromActiveTheme();
    void OnReloadActiveTheme();
    void OnSaveAsNewTheme();
    void OnApplyToEditor();
    void OnThemeComboChanged(int index);

private:
    // Fills the theme selector from the themes pool and selects the active theme (signals blocked).
    void PopulateThemeCombo();

    // Shows/hides token rows (and empty cards) across both tabs by name/value substring.
    void FilterTokens(const QString& text);

private:
    //----------------------------------------------------------------------
    // Static helpers -- inheritance resolution
    //----------------------------------------------------------------------

    // Recursively resolves theme_base inheritance and deep-merges theme_properties.
    static QJsonObject ResolveEffectiveProperties(const QString& themeName, int depth = 0);

    // Deep-merges overlay into target; overlay wins on key collision.
    static void DeepMerge(QJsonObject& target, const QJsonObject& overlay);

    //----------------------------------------------------------------------
    // Card / column layout helpers
    //----------------------------------------------------------------------

    struct CardDef
    {
        QString      m_title;
        QStringList  m_tokens;    // ordered token names for this card
        bool         m_startExpanded = false;
    };

    // Returns the ordered list of card definitions for the Colors tab (General first, Other last).
    // Metric (px) tokens are excluded -- they belong to the Structure tab.
    static QList<CardDef> BuildCardDefs(const QHash<QString, QString>& flat);

    // Returns the card definitions for the Structure tab (Roundness, Sizing) from the metric tokens.
    static QList<CardDef> BuildStructureCardDefs(const QHash<QString, QString>& flat);

    // Builds one collapsible card QFrame for the given CardDef, parented to parentContainer.
    // Each row: [token label] [value label] [color swatch button OR numeric px spin box].
    QFrame* BuildCard(const CardDef& def, QWidget* parentContainer);

    // Distributes the Colors cards across m_columnLayouts based on current width.
    // Chooses column count: width < 700 -> 1, < 1100 -> 2, else 3.
    void ReflowCards(bool force = false);

    // Same responsive reflow for the Structure tab's metric cards.
    void ReflowStructureCards(bool force = false);

    // Shared reflow: distribute the (non-hidden) cards into the first N column layouts by the scroll
    // area's width. force=true re-packs even when the column count is unchanged (used after filtering).
    static void ReflowColumns(
        QScrollArea* scrollArea, QWidget* container, QList<QVBoxLayout*>& columns,
        const QList<QFrame*>& cards, int& currentColumns, bool force);

    //----------------------------------------------------------------------
    // Members
    //----------------------------------------------------------------------

    // Top bar
    QComboBox*   m_themeCombo      = nullptr;   // active-theme selector (switches the live theme)
    QPushButton* m_reloadButton    = nullptr;
    QPushButton* m_saveAsButton    = nullptr;
    QPushButton* m_applyButton     = nullptr;
    QCheckBox*   m_livePreviewCheck = nullptr;

    // Search box (filters tokens across both tabs)
    QLineEdit*   m_searchBox       = nullptr;

    // Tabbed body: Colors | Structure
    QTabWidget*  m_tabs            = nullptr;

    // Colors tab: scroll area + responsive column container
    QScrollArea* m_scrollArea      = nullptr;
    QWidget*     m_columnContainer = nullptr;
    QList<QVBoxLayout*> m_columnLayouts;   // [0..2] column VBoxes inside m_columnContainer
    int          m_currentColumns  = 0;

    // Ordered list of all built color card frames (owned by m_columnContainer via re-parent).
    QList<QFrame*> m_cards;

    // Structure tab: responsive column scroll of metric (roundness/spacing/sizing) cards
    QScrollArea* m_structureScroll    = nullptr;
    QWidget*     m_structureContainer = nullptr;
    QList<QVBoxLayout*> m_structureColumnLayouts;   // [0..2] column VBoxes inside m_structureContainer
    int          m_structureCurrentColumns = 0;
    QList<QFrame*> m_structureCards;

    // Debounce timer for the slow full re-polish (live preview path).
    QTimer* m_applyTimer = nullptr;

    // Source-of-truth for edits: flatTokenName -> current value string.
    QHash<QString, QString> m_effectiveFlat;
};
