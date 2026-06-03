/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorDefs.h"

#include "ThemeEditor/ThemeEditorWidget.h"

// AzToolsFramework
#include <AzToolsFramework/API/ViewPaneOptions.h>       // for AzToolsFramework::ViewPaneOptions
#include <AzToolsFramework/API/ToolsApplicationAPI.h>   // for AzToolsFramework::RegisterViewPane

// AzQtComponents
#include <AzQtComponents/Components/StyleManager.h>     // for AzQtComponents::StyleManager

// Editor
#include "LyViewPaneNames.h"
#include "Settings.h"   // for gSettings (persist the active theme)

// Qt
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QColorDialog>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

//////////////////////////////////////////////////////////////////////////
// RegisterViewClass
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::RegisterViewClass()
{
    AzToolsFramework::ViewPaneOptions options;
    // Open as its own floating window by default (NoDockWidgetArea + an initial rect), rather than
    // docking/tabbing onto an existing pane. A previously-saved layout still takes precedence.
    options.preferedDockingArea = Qt::NoDockWidgetArea;
    options.paneRect = QRect(120, 120, 760, 820);
    AzToolsFramework::RegisterViewPane<ThemeEditorWidget>(WidgetName, LyViewPane::CategoryTools, options);
}

//////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
//////////////////////////////////////////////////////////////////////////
ThemeEditorWidget::ThemeEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    // --- Theme selector (active theme + switch to others) ---
    QLabel* themeSelectLabel = new QLabel(tr("Theme:"), this);
    m_themeCombo = new QComboBox(this);
    m_themeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_themeCombo->setToolTip(tr("Active editor theme. Selecting another theme applies it immediately."));
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, &ThemeEditorWidget::OnThemeComboChanged);

    QHBoxLayout* themeRow = new QHBoxLayout;
    themeRow->addWidget(themeSelectLabel);
    themeRow->addWidget(m_themeCombo, 1);

    // --- Button row ---
    m_reloadButton = new QPushButton(tr("Reload Active Theme"), this);
    m_reloadButton->setToolTip(tr("Reload the active theme from disk, discarding unsaved edits, and re-apply it."));
    connect(m_reloadButton, &QPushButton::clicked, this, &ThemeEditorWidget::OnReloadActiveTheme);

    m_saveAsButton = new QPushButton(tr("Save As New Theme..."), this);
    connect(m_saveAsButton, &QPushButton::clicked, this, &ThemeEditorWidget::OnSaveAsNewTheme);

    m_livePreviewCheck = new QCheckBox(tr("Live preview"), this);
    m_livePreviewCheck->setChecked(false);
    m_livePreviewCheck->setToolTip(tr("When checked, the editor stylesheet is re-applied 300 ms after each color pick."));

    m_applyButton = new QPushButton(tr("Apply to Editor"), this);
    m_applyButton->setToolTip(tr("Re-apply the current token set to the editor stylesheet immediately."));
    connect(m_applyButton, &QPushButton::clicked, this, &ThemeEditorWidget::OnApplyToEditor);

    QHBoxLayout* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_reloadButton);
    buttonRow->addWidget(m_saveAsButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_livePreviewCheck);
    buttonRow->addWidget(m_applyButton);

    // --- Scroll area + responsive column container ---
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_columnContainer = new QWidget;
    m_columnContainer->setObjectName("CardColumnContainer");

    // Pre-build three column VBoxes inside a single HBoxLayout.
    // ReflowCards() will hide/show columns and re-parent cards as needed.
    QHBoxLayout* columnHBox = new QHBoxLayout(m_columnContainer);
    columnHBox->setAlignment(Qt::AlignTop);
    columnHBox->setContentsMargins(4, 4, 4, 4);
    columnHBox->setSpacing(8);

    for (int i = 0; i < 3; ++i)
    {
        QVBoxLayout* col = new QVBoxLayout;
        col->setAlignment(Qt::AlignTop);
        col->setSpacing(6);
        columnHBox->addLayout(col, 1);
        m_columnLayouts.append(col);
    }

    m_scrollArea->setWidget(m_columnContainer);

    // --- Structure tab: single-column scroll of metric (roundness/sizing) cards ---
    m_structureScroll = new QScrollArea(this);
    m_structureScroll->setWidgetResizable(true);
    m_structureScroll->setFrameShape(QFrame::NoFrame);

    m_structureContainer = new QWidget;
    m_structureContainer->setObjectName("StructureColumnContainer");

    m_structureLayout = new QVBoxLayout(m_structureContainer);
    m_structureLayout->setAlignment(Qt::AlignTop);
    m_structureLayout->setContentsMargins(4, 4, 4, 4);
    m_structureLayout->setSpacing(6);

    m_structureScroll->setWidget(m_structureContainer);

    // --- Tab widget wrapping both panes (Colors | Structure) ---
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(m_scrollArea, tr("Colors"));
    m_tabs->addTab(m_structureScroll, tr("Structure"));

    // --- Debounce timer for optional live-preview re-polish ---
    m_applyTimer = new QTimer(this);
    m_applyTimer->setSingleShot(true);
    connect(m_applyTimer, &QTimer::timeout, this, []()
    {
        AzQtComponents::StyleManager::reapplyTheme();
    });

    // --- Top-level layout ---
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(themeRow);
    layout->addLayout(buttonRow);
    layout->addWidget(m_tabs, 1);
    setLayout(layout);

    RebuildFromActiveTheme();
}

ThemeEditorWidget::~ThemeEditorWidget() = default;

//////////////////////////////////////////////////////////////////////////
// STATIC HELPERS -- Inheritance resolution
//////////////////////////////////////////////////////////////////////////

void ThemeEditorWidget::DeepMerge(QJsonObject& target, const QJsonObject& overlay)
{
    for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it)
    {
        const QString& key = it.key();
        if (target.contains(key) && target.value(key).isObject() && it.value().isObject())
        {
            QJsonObject sub = target.value(key).toObject();
            DeepMerge(sub, it.value().toObject());
            target.insert(key, sub);
        }
        else
        {
            target.insert(key, it.value());
        }
    }
}

QJsonObject ThemeEditorWidget::ResolveEffectiveProperties(const QString& themeName, int depth)
{
    if (depth > 8 || themeName.isEmpty())
    {
        return QJsonObject{};
    }

    QFile file(QStringLiteral("THEMES:") + themeName + QStringLiteral("/themeProperties.json"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QJsonObject{};
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QString base = root.value(QStringLiteral("theme_base")).toString();

    QJsonObject result = base.isEmpty() ? QJsonObject{} : ResolveEffectiveProperties(base, depth + 1);
    DeepMerge(result, root.value(QStringLiteral("theme_properties")).toObject());

    return result;
}

//////////////////////////////////////////////////////////////////////////
// FREE HELPERS -- Color parsing / formatting
//////////////////////////////////////////////////////////////////////////

static bool IsColorValue(const QString& value)
{
    return QColor(value).isValid() || value.trimmed().startsWith(QLatin1String("rgb"));
}

// A pixel metric value such as "2px", "0px" or "12px" (optionally negative).
static bool IsMetricValue(const QString& value)
{
    static const QRegularExpression re(QStringLiteral("^-?\\d+px$"));
    return re.match(value.trimmed()).hasMatch();
}

// Parses the integer pixel count out of a metric value ("4px" -> 4).
static int ParseMetricPx(const QString& value)
{
    return value.trimmed().chopped(2).toInt(); // strip trailing "px"
}

static QColor ParseColorValue(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.startsWith(QLatin1String("rgb")))
    {
        static const QRegularExpression re(QStringLiteral("(\\d+)"));
        QRegularExpressionMatchIterator it = re.globalMatch(trimmed);
        int components[4] = { 0, 0, 0, 255 };
        int idx = 0;
        while (it.hasNext() && idx < 4)
        {
            components[idx++] = it.next().captured(1).toInt();
        }
        return QColor(components[0], components[1], components[2], components[3]);
    }
    return QColor(value);
}

static QString FormatColor(const QColor& color)
{
    if (color.alpha() == 255)
    {
        return color.name(); // #rrggbb
    }
    return QString("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

//////////////////////////////////////////////////////////////////////////
// CARD DEFINITION -- Curated category map (order is canonical)
//////////////////////////////////////////////////////////////////////////

// static
QList<ThemeEditorWidget::CardDef> ThemeEditorWidget::BuildCardDefs(const QHash<QString, QString>& flat)
{
    // Curated category map.  Only tokens actually present in flat are included.
    // The "Other (advanced)" card is generated from whatever is left over.
    struct CategorySpec
    {
        const char*              m_title;
        QList<const char*>       m_tokens;
        bool                     m_startExpanded;
    };

    static const CategorySpec k_specs[] =
    {
        { "General", {
            "BackgroundColor", "WindowTextColor", "PrimaryTextColor",
            "SeparatorColor", "SeparatorHoveredColor", "MenuItemSelectedColor",
            "FocusBorderColor", "ErrorColor"
          }, true },
        { "Text", {
            "SecondaryTextColor", "DisabledTextColor", "HighlightTextColor", "BlackTextColor"
          }, false },
        { "Menus", {
            "MenuBackgroundColor", "MenuItemSelectedBackgroundColor", "MenuItemDisabledColor",
            "MenuSeparatorColor", "MenuBarItemSelectedBackgroundColor"
          }, false },
        { "Inputs & Fields", {
            "InputBackgroundColor", "InputTextColor", "InputBorderColor",
            "InputDisabledBackgroundColor", "InputFocusBackgroundColor",
            "ComboBoxHoverBorderColor", "ComboBoxSeparatorColor", "SpinBoxHoverBorderColor",
            "TextEditBackgroundColor", "TextEditTextColor", "TextEditHoverBorderColor",
            "ToolTipBackgroundColor"
          }, false },
        { "Tabs, Docks & Title Bars", {
            "TabWidgetInactiveTabColor", "TabWidgetSecondaryTabTextColor",
            "TabWidgetSecondarySelectedBorderColor", "TabWidgetSecondaryPaneBorderColor",
            "QDockWidgetTitleColor", "StyledDockWidgetFloatingBorderColor",
            "TitleBarBorderColor", "TitleBarCloseButtonHoverColor"
          }, false },
        { "Lists & Tables", {
            "TableViewAlternateRowColor", "TableViewSelectionColor", "TableViewHoverColor",
            "TableViewHeaderColor", "TableViewDisabledItemColor"
          }, false },
        { "Buttons & Controls", {
            "PushButtonPrimaryDisabledColor", "SegmentControlButtonColor",
            "BrowseEditFocusBorderColor", "BrowseEditDisabledBorderColor",
            "ScrollBarHandleHoverColor"
          }, false },
    };

    // Track which tokens we have already claimed so "Other" is the true remainder.
    QSet<QString> claimed;

    QList<CardDef> result;

    for (const CategorySpec& spec : k_specs)
    {
        CardDef card;
        card.m_title          = QString::fromUtf8(spec.m_title);
        card.m_startExpanded  = spec.m_startExpanded;

        for (const char* tok : spec.m_tokens)
        {
            const QString key = QString::fromUtf8(tok);
            if (flat.contains(key))
            {
                card.m_tokens.append(key);
                claimed.insert(key);
            }
        }

        // Always emit the card even if empty so the UI is stable; BuildCard
        // will just render an empty body (rare in practice).
        result.append(card);
    }

    // "Other (advanced)" -- everything not yet claimed, sorted for stable order.
    CardDef other;
    other.m_title         = "Other (advanced)";
    other.m_startExpanded = false;
    QStringList remainder;
    for (auto it = flat.constBegin(); it != flat.constEnd(); ++it)
    {
        // Metric (px) tokens live in the Structure tab, not the Colors "Other" card.
        if (!claimed.contains(it.key()) && !IsMetricValue(it.value()))
        {
            remainder.append(it.key());
        }
    }
    remainder.sort(Qt::CaseInsensitive);
    other.m_tokens = remainder;
    result.append(other);

    return result;
}

//////////////////////////////////////////////////////////////////////////
// STRUCTURE CARD DEFINITION -- metric (px) tokens grouped by kind
//////////////////////////////////////////////////////////////////////////

// static
QList<ThemeEditorWidget::CardDef> ThemeEditorWidget::BuildStructureCardDefs(const QHash<QString, QString>& flat)
{
    QStringList roundness;
    QStringList sizing;
    for (auto it = flat.constBegin(); it != flat.constEnd(); ++it)
    {
        if (!IsMetricValue(it.value()))
        {
            continue;
        }
        if (it.key().startsWith(QLatin1String("Radius")))
        {
            roundness.append(it.key());
        }
        else
        {
            sizing.append(it.key());
        }
    }
    roundness.sort(Qt::CaseInsensitive);
    sizing.sort(Qt::CaseInsensitive);

    QList<CardDef> result;
    if (!roundness.isEmpty())
    {
        CardDef card;
        card.m_title         = "Roundness";
        card.m_tokens        = roundness;
        card.m_startExpanded = true;
        result.append(card);
    }
    if (!sizing.isEmpty())
    {
        CardDef card;
        card.m_title         = "Sizing";
        card.m_tokens        = sizing;
        card.m_startExpanded = false;
        result.append(card);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// CARD BUILDER -- One collapsible card per category
//////////////////////////////////////////////////////////////////////////

QFrame* ThemeEditorWidget::BuildCard(const CardDef& def, QWidget* parentContainer)
{
    // Outer card frame
    QFrame* card = new QFrame(parentContainer);
    card->setObjectName("ThemeEditorCard");
    card->setFrameShape(QFrame::StyledPanel);
    card->setFrameShadow(QFrame::Raised);

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    // --- Clickable header ---
    QToolButton* header = new QToolButton(card);
    header->setText(def.m_title);
    header->setCheckable(true);
    header->setChecked(def.m_startExpanded);
    header->setArrowType(def.m_startExpanded ? Qt::DownArrow : Qt::RightArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->setStyleSheet("QToolButton { text-align: left; font-weight: bold; padding: 4px 6px; }");
    cardLayout->addWidget(header);

    // --- Separator line under header ---
    QFrame* sep = new QFrame(card);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    cardLayout->addWidget(sep);

    // --- Body widget (collapsible) ---
    QWidget* body = new QWidget(card);
    body->setVisible(def.m_startExpanded);

    QFormLayout* form = new QFormLayout(body);
    form->setContentsMargins(8, 4, 8, 8);
    form->setSpacing(4);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Build one row per token.
    for (const QString& flatName : def.m_tokens)
    {
        const QString valueStr = m_effectiveFlat.value(flatName);

        // Left label -- the token name
        QLabel* tokenLabel = new QLabel(flatName, body);
        tokenLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        // Right side: value text + optional color swatch in a horizontal row
        QWidget*     rowWidget = new QWidget(body);
        QHBoxLayout* rowHBox   = new QHBoxLayout(rowWidget);
        rowHBox->setContentsMargins(0, 0, 0, 0);
        rowHBox->setSpacing(4);

        QLabel* valueLabel = new QLabel(valueStr, rowWidget);
        valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        rowHBox->addWidget(valueLabel, 1);

        if (IsColorValue(valueStr))
        {
            QPushButton* swatch = new QPushButton(rowWidget);
            swatch->setFixedSize(36, 16);
            swatch->setFlat(true);
            swatch->setStyleSheet(
                QString("background-color: %1; border: 1px solid #000000;").arg(valueStr));

            connect(swatch, &QPushButton::clicked, this,
                [this, flatName, valueLabel, swatch]()
                {
                    const QString currentValue = m_effectiveFlat.value(flatName);
                    const QColor initial = ParseColorValue(currentValue);

                    const QColor chosen = QColorDialog::getColor(
                        initial, this, tr("Pick Color"),
                        QColorDialog::ShowAlphaChannel);

                    if (!chosen.isValid())
                    {
                        return;
                    }

                    const QString newValue = FormatColor(chosen);

                    // Update swatch + value label immediately.
                    valueLabel->setText(newValue);
                    swatch->setStyleSheet(
                        QString("background-color: %1; border: 1px solid #000000;").arg(newValue));

                    // Update the flat edit map and push the single token to StyleManager.
                    m_effectiveFlat[flatName] = newValue;
                    AzQtComponents::StyleManager::setThemeProperty(flatName, newValue);

                    // Trigger a full re-polish only if live preview is enabled.
                    if (m_livePreviewCheck->isChecked())
                    {
                        m_applyTimer->start(300);
                    }
                });

            rowHBox->addWidget(swatch);
        }
        else if (IsMetricValue(valueStr))
        {
            QSpinBox* spin = new QSpinBox(rowWidget);
            spin->setRange(0, 64);
            spin->setSuffix(QStringLiteral("px"));
            spin->setValue(ParseMetricPx(valueStr));   // set before connecting to avoid a spurious edit

            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this, flatName, valueLabel](int px)
                {
                    const QString newValue = QString::number(px) + QStringLiteral("px");

                    // Update the value label, the flat edit map, and push the single token.
                    valueLabel->setText(newValue);
                    m_effectiveFlat[flatName] = newValue;
                    AzQtComponents::StyleManager::setThemeProperty(flatName, newValue);

                    // Trigger a full re-polish only if live preview is enabled.
                    if (m_livePreviewCheck->isChecked())
                    {
                        m_applyTimer->start(300);
                    }
                });

            rowHBox->addWidget(spin);
        }

        form->addRow(tokenLabel, rowWidget);
    }

    cardLayout->addWidget(body);

    // Toggle body visibility and arrow when header is clicked.
    connect(header, &QToolButton::toggled, this, [body, header](bool checked)
    {
        body->setVisible(checked);
        header->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    });

    return card;
}

//////////////////////////////////////////////////////////////////////////
// RESPONSIVE REFLOW -- Distribute cards into 1/2/3 columns by widget width
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::ReflowCards()
{
    // Choose column count by available width (account for scroll bar).
    const int w = m_scrollArea->viewport()->width();
    const int targetColumns = (w < 700) ? 1 : (w < 1100) ? 2 : 3;

    if (targetColumns == m_currentColumns && !m_cards.isEmpty())
    {
        return; // No change needed.
    }
    m_currentColumns = targetColumns;

    // Remove all card widgets from every column layout without destroying them.
    for (QVBoxLayout* col : m_columnLayouts)
    {
        while (col->count() > 0)
        {
            QLayoutItem* item = col->takeAt(0);
            if (item->widget())
            {
                item->widget()->setParent(nullptr); // detach, do not delete
            }
            delete item;
        }
    }

    // Show/hide column layouts by toggling stretch factor is not enough;
    // use a spacer in unused columns to collapse them visually.
    // Simpler: just distribute into the first N columns.
    const int total = m_cards.size();
    for (int i = 0; i < total; ++i)
    {
        const int col = i % targetColumns;
        m_cards[i]->setParent(m_columnContainer);
        m_columnLayouts[col]->addWidget(m_cards[i]);
    }

    // Add a vertical stretch to the active columns and remove from inactive ones.
    for (int c = 0; c < 3; ++c)
    {
        // If fewer columns are active, add an invisible stretch so the layout
        // does not expand unused columns.
        if (c >= targetColumns)
        {
            m_columnLayouts[c]->addStretch(1);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// resizeEvent -- trigger reflow when widget is resized
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    ReflowCards();
}

//////////////////////////////////////////////////////////////////////////
// showEvent -- reflow once the widget has its real (docked) width
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // At construction the widget has no meaningful width, so the initial reflow defaults
    // to a single column. Reflow once after the real size is known.
    QTimer::singleShot(0, this, [this]() { ReflowCards(); });
}

//////////////////////////////////////////////////////////////////////////
// RebuildFromActiveTheme
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::RebuildFromActiveTheme()
{
    // Tear down old cards.
    for (QFrame* card : m_cards)
    {
        card->setParent(nullptr);
        delete card;
    }
    m_cards.clear();
    m_effectiveFlat.clear();
    m_currentColumns = 0; // Force a reflow after rebuild.

    // Tear down old structure cards + any trailing stretch.
    for (QFrame* card : m_structureCards)
    {
        delete card;
    }
    m_structureCards.clear();
    while (QLayoutItem* item = m_structureLayout->takeAt(0))
    {
        delete item;
    }

    // Refresh the theme selector (picks up any newly-saved themes and selects the active one).
    PopulateThemeCombo();

    const QString themeName = AzQtComponents::StyleManager::currentThemeName();
    if (themeName.isEmpty())
    {
        return;
    }

    // Resolve the full effective token set (including base-theme inheritance).
    const QJsonObject effectiveProps = ResolveEffectiveProperties(themeName);

    // Flatten the resolved JSON object into m_effectiveFlat (leaf values only).
    // The resolved props from this codebase are already a flat object after
    // deep-merge (no nested objects at runtime), but we handle nesting for safety.
    std::function<void(const QJsonObject&, const QString&)> flattenJson =
        [&](const QJsonObject& obj, const QString& prefix)
        {
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            {
                const QString key = prefix + it.key();
                if (it.value().isObject())
                {
                    flattenJson(it.value().toObject(), key);
                }
                else
                {
                    m_effectiveFlat.insert(key, it.value().toString());
                }
            }
        };
    flattenJson(effectiveProps, QString{});

    // Build curated card definitions and create card frames.
    const QList<CardDef> defs = BuildCardDefs(m_effectiveFlat);
    for (const CardDef& def : defs)
    {
        m_cards.append(BuildCard(def, m_columnContainer));
    }

    // Distribute color cards into columns.
    ReflowCards();

    // Build the Structure tab (roundness / sizing) from the metric tokens.
    const QList<CardDef> structDefs = BuildStructureCardDefs(m_effectiveFlat);
    for (const CardDef& def : structDefs)
    {
        QFrame* card = BuildCard(def, m_structureContainer);
        m_structureCards.append(card);
        m_structureLayout->addWidget(card);
    }
    m_structureLayout->addStretch(1);
}

//////////////////////////////////////////////////////////////////////////
// OnApplyToEditor
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::OnApplyToEditor()
{
    AzQtComponents::StyleManager::reapplyTheme();
}

//////////////////////////////////////////////////////////////////////////
// OnReloadActiveTheme -- discard unsaved edits, restore the on-disk theme
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// Theme selector
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::PopulateThemeCombo()
{
    const QSignalBlocker blocker(m_themeCombo);
    m_themeCombo->clear();

    const QString current = AzQtComponents::StyleManager::currentThemeName();
    int currentIndex = -1;
    const auto themes = AzQtComponents::StyleManager::availableThemes();
    for (const auto& theme : themes)
    {
        m_themeCombo->addItem(theme.displayName, theme.folderName);
        if (theme.folderName == current)
        {
            currentIndex = m_themeCombo->count() - 1;
        }
    }
    if (currentIndex >= 0)
    {
        m_themeCombo->setCurrentIndex(currentIndex);
    }
}

void ThemeEditorWidget::OnThemeComboChanged(int index)
{
    if (index < 0)
    {
        return;
    }
    const QString folderName = m_themeCombo->itemData(index).toString();
    if (folderName.isEmpty() || folderName == AzQtComponents::StyleManager::currentThemeName())
    {
        return;
    }

    // Switch + apply the selected theme, and persist the choice (kept in sync with Global Preferences).
    AzQtComponents::StyleManager::setTheme(folderName);
    gSettings.gui.editorTheme = folderName.toUtf8().constData();

    // Rebuild the token cards for the newly active theme.
    RebuildFromActiveTheme();
}

void ThemeEditorWidget::OnReloadActiveTheme()
{
    // setTheme clears the live override map, reloads the on-disk theme, and re-applies it --
    // so this discards any unsaved color edits made this session and truly restores the theme.
    const QString themeName = AzQtComponents::StyleManager::currentThemeName();
    if (!themeName.isEmpty())
    {
        AzQtComponents::StyleManager::setTheme(themeName);
    }

    // Refresh the display from the now-clean theme.
    RebuildFromActiveTheme();
}

//////////////////////////////////////////////////////////////////////////
// OnSaveAsNewTheme
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::OnSaveAsNewTheme()
{
    // --- Get a name from the user ---
    bool ok = false;
    const QString rawName = QInputDialog::getText(
        this,
        tr("Save As New Theme"),
        tr("Theme folder name (letters, digits, underscore):"),
        QLineEdit::Normal,
        QString{},
        &ok);

    if (!ok || rawName.trimmed().isEmpty())
    {
        return;
    }

    // Sanitize: keep only word characters (letters / digits / underscore).
    static const QRegularExpression sanitize(QStringLiteral("[^\\w]"));
    const QString safeName = rawName.trimmed().replace(sanitize, QStringLiteral("_"));

    if (safeName.isEmpty())
    {
        QMessageBox::warning(this, tr("Save As New Theme"), tr("The name you entered is not valid."));
        return;
    }

    // --- Build the output path ---
    const QString themesRoot = AzQtComponents::StyleManager::themesRootPath();
    const QString themeDir   = themesRoot + QStringLiteral("/") + safeName;
    const QString filePath   = themeDir   + QStringLiteral("/themeProperties.json");

    if (!QDir().mkpath(themeDir))
    {
        QMessageBox::critical(this, tr("Save As New Theme"),
            tr("Could not create directory:\n%1").arg(themeDir));
        return;
    }

    // --- Build the flat theme_properties object from m_effectiveFlat ---
    QJsonObject flatProps;
    for (auto it = m_effectiveFlat.constBegin(); it != m_effectiveFlat.constEnd(); ++it)
    {
        flatProps.insert(it.key(), it.value());
    }

    QJsonObject root;
    root.insert(QStringLiteral("theme_id"),         safeName.toLower());
    root.insert(QStringLiteral("theme_name"),        safeName);
    root.insert(QStringLiteral("theme_description"), QStringLiteral("Authored in the Theme Editor."));
    root.insert(QStringLiteral("theme_base"),        QString{});
    root.insert(QStringLiteral("theme_properties"),  flatProps);

    // --- Write the file ---
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::critical(this, tr("Save As New Theme"),
            tr("Could not write file:\n%1").arg(filePath));
        return;
    }

    file.write(QJsonDocument(root).toJson());
    file.close();

    // The new theme is now in the pool -- refresh the selector so it appears.
    PopulateThemeCombo();

    QMessageBox::information(this, tr("Save As New Theme"),
        tr("Theme \"%1\" saved.\n\nIt will appear in Global Preferences > General > Editor Theme "
           "the next time that combo is opened.").arg(safeName));
}
