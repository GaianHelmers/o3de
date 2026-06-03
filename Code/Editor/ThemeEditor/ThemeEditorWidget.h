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
class QFrame;
class QLabel;
class QPushButton;
class QScrollArea;
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

private slots:
    void RebuildFromActiveTheme();
    void OnSaveAsNewTheme();
    void OnApplyToEditor();

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

    // Returns the ordered list of card definitions (General first, Other last).
    static QList<CardDef> BuildCardDefs(const QHash<QString, QString>& flat);

    // Builds one collapsible card QFrame for the given CardDef.
    // Each row in the body: [token label] [value label] [color swatch button]
    QFrame* BuildCard(const CardDef& def);

    // Distributes m_cards across m_columnLayouts based on current width.
    // Chooses column count: width < 700 -> 1, < 1100 -> 2, else 3.
    void ReflowCards();

    //----------------------------------------------------------------------
    // Members
    //----------------------------------------------------------------------

    // Top bar
    QLabel*      m_themeNameLabel  = nullptr;
    QPushButton* m_reloadButton    = nullptr;
    QPushButton* m_saveAsButton    = nullptr;
    QPushButton* m_applyButton     = nullptr;
    QCheckBox*   m_livePreviewCheck = nullptr;

    // Scroll area + column container
    QScrollArea* m_scrollArea      = nullptr;
    QWidget*     m_columnContainer = nullptr;
    QList<QVBoxLayout*> m_columnLayouts;   // [0..2] column VBoxes inside m_columnContainer
    int          m_currentColumns  = 0;

    // Ordered list of all built card frames (owned by m_columnContainer via re-parent).
    QList<QFrame*> m_cards;

    // Debounce timer for the slow full re-polish (live preview path).
    QTimer* m_applyTimer = nullptr;

    // Source-of-truth for edits: flatTokenName -> current value string.
    QHash<QString, QString> m_effectiveFlat;
};
