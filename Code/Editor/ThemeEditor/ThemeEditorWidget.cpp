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
#include "ThemeEditor/ThemeWorkingOverrides.h"   // persist Applied (unnamed) edits across restart

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
#include <QLineEdit>
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

    // --- Structure tab: responsive column scroll of metric (roundness/spacing/sizing) cards ---
    m_structureScroll = new QScrollArea(this);
    m_structureScroll->setWidgetResizable(true);
    m_structureScroll->setFrameShape(QFrame::NoFrame);

    m_structureContainer = new QWidget;
    m_structureContainer->setObjectName("StructureColumnContainer");

    // Three column VBoxes inside one HBox, mirroring the Colors tab; ReflowStructureCards() distributes.
    QHBoxLayout* structureHBox = new QHBoxLayout(m_structureContainer);
    structureHBox->setAlignment(Qt::AlignTop);
    structureHBox->setContentsMargins(4, 4, 4, 4);
    structureHBox->setSpacing(8);

    for (int i = 0; i < 3; ++i)
    {
        QVBoxLayout* col = new QVBoxLayout;
        col->setAlignment(Qt::AlignTop);
        col->setSpacing(6);
        structureHBox->addLayout(col, 1);
        m_structureColumnLayouts.append(col);
    }

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

    // --- Search box (filters tokens across both tabs by name or value) ---
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Search tokens by name or value..."));
    m_searchBox->setClearButtonEnabled(true);
    connect(m_searchBox, &QLineEdit::textChanged, this, &ThemeEditorWidget::FilterTokens);

    // --- Top-level layout ---
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(themeRow);
    layout->addLayout(buttonRow);
    layout->addWidget(m_searchBox);
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

// A pixel metric value: 1 to 4 space-separated "px" components (each optionally negative).
// e.g. "2px" (single), "2px 4px" (vertical horizontal), "8px 7px 7px 7px" (top right bottom left).
static bool IsMetricValue(const QString& value)
{
    static const QRegularExpression re(QStringLiteral("^-?\\d+px( +-?\\d+px){0,3}$"));
    return re.match(value.trimmed()).hasMatch();
}

// Splits a metric value into its integer pixel components ("8px 7px 7px 7px" -> [8,7,7,7]).
static QList<int> SplitMetricPx(const QString& value)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QList<int> out;
    for (const QString& part : value.trimmed().split(ws, Qt::SkipEmptyParts))
    {
        out.append(part.endsWith(QStringLiteral("px")) ? part.chopped(2).toInt() : part.toInt());
    }
    return out;
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
            "FocusBorderColor", "ErrorColor",
            "WindowBackgroundColor", "PanelBackgroundColor", "DarkPanelBackgroundColor",
            "WidgetBackgroundColor", "GraphCanvasBackgroundColor", "GraphCanvasNodeBackgroundColor"
          }, true },
        { "Text", {
            "SecondaryTextColor", "DisabledTextColor", "HighlightTextColor", "BlackTextColor",
            "LabelTextColor", "LinkColor", "LinkHoverColor",
            "TextErrorColor", "TextPositiveColor", "TextWarningColor", "TextInfoColor",
            "OverrideHighlightColor"
          }, false },
        { "Typography", {
            "FontFamily", "AmazonEmberFontFamily"
          }, false },
        { "Menus", {
            "MenuBackgroundColor", "MenuItemSelectedBackgroundColor", "MenuItemDisabledColor",
            "MenuSeparatorColor", "MenuSeparatorTopColor", "MenuBorderColor",
            "MenuBarBackgroundColor", "MenuBarItemSelectedBackgroundColor",
            "MenuBarItemSelectedColor", "GlobalMenuItemSelectedColor"
          }, false },
        { "Inputs & Fields", {
            "InputBackgroundColor", "InputTextColor", "InputBorderColor",
            "InputDisabledBackgroundColor", "InputFocusBackgroundColor",
            "ComboBoxHoverBorderColor", "ComboBoxSeparatorColor",
            "ComboBoxDisabledTextColor", "ComboBoxDisabledBackgroundColor",
            "SpinBoxHoverBorderColor", "SpinBoxDisabledBackgroundColor",
            "TextEditBackgroundColor", "TextEditTextColor", "TextEditHoverBorderColor",
            "TextEditDarkBackgroundColor", "ToolTipBackgroundColor",
            "SearchInputBackgroundColor", "SearchLineEditErrorColor", "SearchLineEditNormalColor",
            "InputInvalidBackgroundColor", "InputInvalidFocusBackgroundColor",
            "BrowseEditFocusBorderColor", "BrowseEditDisabledBorderColor",
            "BrowseEditButtonGradientTop", "BrowseEditButtonGradientBottom",
            "BrowseEditButtonHoverGradientTop", "BreadCrumbsSeparatorColor",
            "BreadCrumbsEditableBorderColor"
          }, false },
        { "Vectors & Axes", {
            "VectorInputXColor", "VectorInputYColor", "VectorInputZColor", "VectorInputWColor",
            "VectorEditBackgroundColor", "VectorEditFocusBackgroundColor",
            "VectorXAxisColor", "VectorYAxisColor", "VectorZAxisColor",
            "VectorFlavorInformationColor", "VectorFlavorInvalidColor", "VectorFlavorValidColor"
          }, false },
        { "Tabs, Docks & Title Bars", {
            "TabWidgetInactiveTabColor", "TabWidgetSecondaryTabTextColor",
            "TabWidgetSecondarySelectedBorderColor", "TabWidgetSecondaryPaneBorderColor",
            "TabWidgetSecondaryHoverColor",
            "QDockWidgetTitleColor", "DockWindowTitleBarColor", "StyledDockWidgetFloatingBorderColor",
            "TitleBarBorderColor", "TitleBarButtonHoverColor", "TitleBarCloseButtonHoverColor",
            "TitleBarSimpleBackgroundColor", "TitleBarLogoColor"
          }, false },
        { "Buttons & Controls", {
            "PushButtonPrimaryDisabledColor", "PushButtonDisabledColor",
            "PushButtonSecondaryNormalStartColor", "PushButtonSecondaryNormalEndColor",
            "PushButtonSecondaryHoveredStartColor", "PushButtonSecondaryHoveredEndColor",
            "PushButtonSecondarySunkenStartColor", "PushButtonSecondarySunkenEndColor",
            "PushButtonSecondaryDisabledStartColor", "PushButtonSecondaryDisabledEndColor",
            "SegmentControlButtonColor", "CheckBoxDisabledColor", "CheckBoxDisabledBorderColor",
            "TinyButtonBorderColor", "TinyButtonCheckedBorderColor",
            "StyledSliderGrooveColor", "SliderHandleColor", "SliderGrooveColor", "SliderGrooveHoveredColor",
            "SliderGradientGrooveBorderColor", "SliderGradientHandleBorderColor",
            "ProgressBarTrackColor", "ProgressBarFillColor",
            "ResourceGroupHighlightColor", "SingleRequiredSelectionBorderColor"
          }, false },
        { "Tool Buttons", {
            "ToolButtonPressedBackgroundColor", "ToolButtonPressedBorderColor",
            "ToolButtonHoverBackgroundColor", "ToolButtonHoverBorderColor",
            "ToolButtonCheckedBackgroundColor"
          }, false },
        { "Scrollbars", {
            "ScrollBarBorderColor", "ScrollBarHandleBorderColor", "ScrollBarHandleColor",
            "ScrollBarDarkHandleColor", "ScrollBarHandleHoverColor", "ScrollBarTrackHoverColor",
            "ScrollBarDarkTrackHoverColor", "ScrollbarBackgroundColor", "ScrollbarBorderColor",
            "ScrollbarHandleBackgroundColor", "ScrollbarHandleBorderColor",
            "GlobalScrollBarBackgroundColor"
          }, false },
        { "Lists & Tables", {
            "TableViewAlternateRowColor", "TableViewSelectionColor", "TableViewHoverColor",
            "TableViewHeaderColor", "TableViewDisabledItemColor",
            "AbstractItemViewAlternateBackgroundColor", "AbstractItemViewTextColor"
          }, false },
        { "Asset Browser", {
            "AssetGridBackgroundColor", "AssetBrowserRootBackgroundColor",
            "AssetBrowserPreviewBackgroundColor", "AssetBrowserSearchBarBackgroundColor",
            "AssetThumbnailRootBackgroundColor", "AssetThumbnailRootBorderColor",
            "AssetThumbnailChildBackgroundColor", "AssetThumbnailChildBorderColor",
            "AssetThumbnailChildFrameBackgroundColor", "AssetThumbnailExpandButtonColor",
            "AssetThumbnailSelectedBorderColor",
            "AssetEditorHeaderBackgroundColor", "AssetEditorBodyBackgroundColor",
            "AssetImporterLabelColor", "ResourceImporterListItemBackgroundColor"
          }, false },
        { "Cards & Components", {
            "CardBackgroundColor", "CardHeaderColor", "CardModifiedTextColor", "CardSelectedBorderColor",
            "ComponentPanelFrameColor", "ComponentEditorBorderColor",
            "ComponentEditorNotificationBackgroundColor", "ComponentEditorNotificationBorderColor",
            "ComponentPaletteWidgetBorderColor", "ComponentPaletteWidgetBackgroundColor",
            "ComponentPaletteTreeSelectionColor"
          }, false },
        { "Color Picker", {
            "ColorPickerModifiedTitleColor", "ColorPickerSeparatorBorderColor",
            "ColorPickerSwatchBorderColor", "ColorPickerSwatchSelectedBorderColor"
          }, false },
        { "Slices & Prefabs", {
            "SliceRootBackgroundColor", "SliceRootNameColor", "SelectedSliceRootBackgroundColor",
            "SelectedSliceRootBorderColor", "SliceEntityColor", "SliceOverrideColor",
            "SliceWarningColor", "SlicePushWarningBottomColor", "SlicePushWarningTreeSelectedColor",
            "HierarchyLinesSlices", "HierarchyLinesSlicesSelected",
            "HierarchyLinesNonSliceEntities", "HierarchyLinesNonSliceEntitiesSelected"
          }, false },
        { "Layers", {
            "LayerBackgroundColor", "LayerChildBackgroundColor", "LayerBorderTop", "LayerBorderBottom",
            "LayerMenuSelected", "LayerMenuDisabled", "NewLayerDefaultColor",
            "LayerBGSelectionColor", "LayerChildBGSelectionColor"
          }, false },
        { "Outliner & Selection", {
            "OutlinerSelectionColor", "OutlinerSearchBackgroundColor",
            "OutlinerSelectionBackgroundColor", "OutlinerHoverBackgroundColor",
            "OutlinerMixedStateIndicatorColor",
            "SelectionHighlightColor", "SearchSelectionBackgroundColor"
          }, false },
        { "Viewport", {
            "ViewportTitleDlgBackgroundColor", "ViewportTitleSearchBackgroundUrl"
          }, false },
        { "Status Bar & Console", {
            "StatusBarBackgroundColor", "StatusBarTextColor", "StatusBarSpacerColor",
            "ConsoleBackgroundColor"
          }, false },
        { "Filters", {
            "FilterCriteriaButtonBorderColor", "FilterCriteriaButtonBackgroundColor",
            "FilterClearLabelColor"
          }, false },
        { "Preferences & Dividers", {
            "PreferencesTreeBackgroundColor", "PreferencesPropertyLabelColor",
            "PropertySectionDividerColor"
          }, false },
        { "Dialogs (Login / Welcome / Survey)", {
            "NetPromoterDialogBackgroundColor", "NetPromoterRatingButtonBackgroundColor",
            "NetPromoterRatingButtonHoverColor", "NetPromoterRatingButtonPressColor",
            "NetPromoterCommentBoxBeforeColor", "NetPromoterCommentBoxAfterColor",
            "LoginDialogBackgroundColor", "LoginDialogTextColor", "LoginDialogFrameBorderColor",
            "WelcomeScreenLinkHoverColor", "WelcomeScreenPinnedArticleBackgroundColor",
            "WelcomeScreenPinnedArticleBorderColor", "WelcomeScreenArticleRootGradient",
            "AddDeploymentLinkColor", "AddDeploymentLinkHoverColor",
            "NoChangesOverlayColor", "NoChangesOverlayTextColor"
          }, false },
        { "Legacy (CryTooltip / Table)", {
            "CToolTipText", "CToolTipBackground", "CTableRowOdd", "CTableRowEven"
          }, false },
        // Class Creation Wizard (standalone PySide6 tool). Its color surface, exposed here so the
        // wizard can be themed alongside the editor. Tokens flatten from the ClassWizard group in
        // each theme's themeProperties.json. Metric tokens (radii) live on the Structure tab.
        { "Class Wizard", {
            "ClassWizardWindowBackgroundColor", "ClassWizardInputBackgroundColor",
            "ClassWizardBorderColor", "ClassWizardTextColor", "ClassWizardDisabledTextColor",
            "ClassWizardOnAccentTextColor", "ClassWizardAccentColor", "ClassWizardAccentHoverColor",
            "ClassWizardAccentPressedColor", "ClassWizardDisabledBackgroundColor"
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
// STRUCTURE CARD DEFINITION -- metric (px) tokens grouped by WIDGET / feature
//////////////////////////////////////////////////////////////////////////

// static
QList<ThemeEditorWidget::CardDef> ThemeEditorWidget::BuildStructureCardDefs(const QHash<QString, QString>& flat)
{
    // Curated WIDGET / feature categories so a widget's size + width + padding + radius + border all sit
    // together (e.g. all spin-box metrics under "Spin Box"). Only metric tokens present in flat are shown;
    // anything not claimed lands in a sorted "Other (metrics)" card.
    struct CategorySpec
    {
        const char*        m_title;
        QList<const char*> m_tokens;
        bool               m_startExpanded;
    };

    static const CategorySpec k_specs[] =
    {
        { "Inputs (shared)", {
            "RadiusInput", "SizeInputHeight", "BorderControl", "BorderControlEmphasis",
            "SpacingInputPadTop", "SpacingInputPadBottom"
          }, true },
        { "Frames & Dividers", { "BorderThin", "BorderDivider" }, false },
        { "Spin Box", { "SpacingSpinBoxPad", "SizeSpinButtonW", "SizeSpinButtonH" }, false },
        { "Combo Box", {
            "SpacingComboItemH", "SpacingComboItemPad", "SpacingComboArrowMarginRight",
            "SpacingComboInMenu", "SpacingComboInMenuHover"
          }, false },
        { "Line Edit", { "SpacingLineEditPadLeftIcon" }, false },
        { "Browse Edit", { "SpacingBrowseEditPad" }, false },
        { "Vector Input", { "SizeVectorHoverHeight" }, false },
        { "Buttons", { "RadiusButton", "RadiusButtonSmall" }, false },
        { "Checkboxes, Radios & Toggles", {
            "SizeToggleW", "SizeToggleFocusW", "LineHeightBase", "LineHeightRadio",
            "SpacingControlMargin", "SpacingControlMarginTight"
          }, false },
        { "Icons & Glyphs", {
            "SizeGlyph", "SizeGlyphTiny", "SizeGlyphSmall", "SizeGlyphMedium",
            "SizeGlyphLarge", "SizeGlyphHuge", "SizeGlyphClose"
          }, false },
        { "Cards", {
            "RadiusCard", "RadiusCardFrame", "BorderCardShadow", "SpacingCardContent",
            "SpacingCardContentPadding", "SpacingCardHeaderPad", "SpacingCardSecondaryHeaderPad",
            "SpacingCardHeaderItemMargin", "SpacingCardIconGap", "SpacingCardMarginBottom",
            "SpacingCardNotificationH", "SpacingCardShadowPad", "SizeCardContextMenuMaxW"
          }, false },
        { "Tabs", {
            "RadiusTabTop", "BorderTabUnderline", "SpacingTabPadding", "SpacingTabCloseGap",
            "SpacingTabSecondaryPadding", "SpacingTabSecondaryBorderedPadding", "SpacingTabSecondaryPanePad",
            "SpacingTabEmptyPaneMarginTop", "SpacingActionToolBarBottom",
            "SizeTabBarHeight", "SizeTabBarSecondaryHeight", "SizeTabMaxWidth"
          }, false },
        { "Menus & Menu Bar", {
            "SizeMenuItemHeight", "SizeMenuBarHeight", "SpacingMenuItemTop", "SpacingMenuItemBottom",
            "SpacingMenuItemLeft", "SpacingMenuBarItemH", "SpacingMenuIndicatorMarginLeft",
            "SpacingMenuSeparatorMarginTop", "SpacingMenuSeparatorMarginBottom",
            "SpacingSourceControlMenuMarginH", "SpacingSourceControlMenuItemMarginLeft"
          }, false },
        { "Scrollbars", { "RadiusScrollHandle", "SizeScrollBarThickness", "SizeScrollHandleMin" }, false },
        { "Segment Control", {
            "RadiusSegment", "SizeSegmentHeight", "SizeSegmentMinWidth", "SpacingSegmentH",
            "SpacingSegmentMarginBottom", "SpacingSegmentOverlap"
          }, false },
        { "Toolbars", {
            "SpacingToolBar", "SpacingToolBarSeparatorMargin", "SpacingToolBarHandleH",
            "SpacingToolBarHandleHLarge", "SpacingToolBarHandleHNormal", "SpacingToolBarHandleV",
            "SpacingToolBarHandleVNeg"
          }, false },
        { "Title Bar", {
            "SizeTitleBarButton", "SizeTitleBarButtonSmall", "SizeTitleBarButtonTab",
            "SpacingTitleBarTitleMargin", "SpacingTitleBarTabButtonsMarginBottom", "FontSizeTitleBar"
          }, false },
        { "Tables & Trees", {
            "SizeRowHeight", "SpacingTableItemPadLeft", "SpacingTableHeaderPadLeft",
            "SpacingTreeIndicatorMarginLeft", "SpacingTreeBranchClosed", "SpacingTreeBranchOpen"
          }, false },
        { "Search & Filter", {
            "RadiusChip", "SizeSearchFieldHeight", "SizeSearchDefaultWidth", "SpacingSearchTagGap"
          }, false },
        { "Color Picker & Swatch", {
            "SizeColorSwatchWidth", "SizeColorComponentWidth", "SpacingColorGridPad"
          }, false },
        { "Tooltip", { "SpacingToolTipV", "SpacingToolTipMarginLeft" }, false },
        { "Message Box", { "SpacingMessageBox" }, false },
        { "Progress Bar", { "SizeProgressBarHeight" }, false },
        { "Breadcrumbs", { "SizeBreadCrumbIconW", "SizeBreadCrumbIconH" }, false },
        { "Typography", {
            "FontSize", "FontSizeHeadline", "FontSizeTitle", "FontSizeSubtitle", "LineHeightTall",
            "SpacingTextHeadlineV", "SpacingTextTitleV", "SpacingTextSubtitleV", "SpacingTextMenuV",
            "SpacingTextParagraphV", "SpacingTextButtonV", "SpacingTextLabelV", "SpacingTextTooltipV"
          }, false },
        { "Property Editor", { "SpacingReflectedPropMarginLeft" }, false },
        // Class Creation Wizard structural metrics (control + group-box corner radii). Paired with
        // the "Class Wizard" color category on the Colors tab.
        { "Class Wizard", { "ClassWizardControlRadius", "ClassWizardGroupBoxRadius" }, false },
    };

    QSet<QString>  claimed;
    QList<CardDef> result;

    for (const CategorySpec& spec : k_specs)
    {
        CardDef card;
        card.m_title         = QString::fromUtf8(spec.m_title);
        card.m_startExpanded = spec.m_startExpanded;
        for (const char* tok : spec.m_tokens)
        {
            const QString key = QString::fromUtf8(tok);
            if (flat.contains(key) && IsMetricValue(flat.value(key)))
            {
                card.m_tokens.append(key);
                claimed.insert(key);
            }
        }
        if (!card.m_tokens.isEmpty())
        {
            result.append(card);
        }
    }

    // Any metric token not claimed above -> sorted "Other (metrics)" card (safety net).
    CardDef other;
    other.m_title         = "Other (metrics)";
    other.m_startExpanded = false;
    QStringList remainder;
    for (auto it = flat.constBegin(); it != flat.constEnd(); ++it)
    {
        if (IsMetricValue(it.value()) && !claimed.contains(it.key()))
        {
            remainder.append(it.key());
        }
    }
    remainder.sort(Qt::CaseInsensitive);
    other.m_tokens = remainder;
    if (!other.m_tokens.isEmpty())
    {
        result.append(other);
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
            // 1 / 2 / 4 px components -> single spin / Vector2 (V H) / Vector4 (T R B L).
            const QList<int> comps = SplitMetricPx(valueStr);

            // Axis labels by component count (CSS shorthand order).
            static const QStringList k2{ "V", "H" };
            static const QStringList k3{ "T", "H", "B" };
            static const QStringList k4{ "T", "R", "B", "L" };
            const QStringList& axis =
                (comps.size() == 4) ? k4 : (comps.size() == 3) ? k3 : (comps.size() == 2) ? k2 : QStringList();

            // Host widget carrying the component spin boxes (found in construction order when recomposing).
            QWidget*     vecWidget = new QWidget(rowWidget);
            QHBoxLayout* vecLayout = new QHBoxLayout(vecWidget);
            vecLayout->setContentsMargins(0, 0, 0, 0);
            vecLayout->setSpacing(4);

            for (int i = 0; i < comps.size(); ++i)
            {
                if (i < axis.size())
                {
                    QLabel* axisLabel = new QLabel(axis[i], vecWidget);
                    axisLabel->setObjectName(QStringLiteral("AxisLabel"));
                    vecLayout->addWidget(axisLabel);
                }
                QSpinBox* spin = new QSpinBox(vecWidget);
                spin->setRange(-64, 64); // metrics can be negative (alignment tweaks)
                spin->setSuffix(QStringLiteral("px"));
                spin->setValue(comps[i]);   // set before connecting to avoid a spurious edit
                // Fixed width so the global QSpinBox hover style (margin 2px->1px) cannot resize it on
                // hover inside the editor's form layout -- the Theme-Editor-only resize the user saw.
                spin->setFixedWidth(60);

                connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this, flatName, valueLabel, vecWidget](int)
                    {
                        // Recompose all components (left-to-right = construction order) into one value.
                        QStringList parts;
                        for (QSpinBox* sp : vecWidget->findChildren<QSpinBox*>())
                        {
                            parts << (QString::number(sp->value()) + QStringLiteral("px"));
                        }
                        const QString newValue = parts.join(QLatin1Char(' '));

                        valueLabel->setText(newValue);
                        m_effectiveFlat[flatName] = newValue;
                        AzQtComponents::StyleManager::setThemeProperty(flatName, newValue);

                        // Trigger a full re-polish only if live preview is enabled.
                        if (m_livePreviewCheck->isChecked())
                        {
                            m_applyTimer->start(300);
                        }
                    });

                vecLayout->addWidget(spin);
            }
            vecLayout->addStretch(1);

            rowHBox->addWidget(vecWidget);
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
void ThemeEditorWidget::ReflowColumns(
    QScrollArea* scrollArea, QWidget* container, QList<QVBoxLayout*>& columns,
    const QList<QFrame*>& cards, int& currentColumns, bool force)
{
    // Choose column count by available width (account for scroll bar).
    const int w = scrollArea->viewport()->width();
    const int targetColumns = (w < 700) ? 1 : (w < 1100) ? 2 : 3;

    if (!force && targetColumns == currentColumns && !cards.isEmpty())
    {
        return; // No change needed.
    }
    currentColumns = targetColumns;

    // Remove all card widgets (and any stretch spacers) from every column without destroying the cards.
    for (QVBoxLayout* col : columns)
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

    // Distribute the cards round-robin into the first N columns. (setParent() hides a widget as a side
    // effect, so we must NOT test isHidden() here -- every freshly built/re-parented card would look
    // hidden. Search-filtered cards are setVisible(false) by FilterTokens and a QBoxLayout already gives
    // hidden widgets zero size, so the visible cards pack with no gaps.)
    for (int i = 0; i < cards.size(); ++i)
    {
        cards[i]->setParent(container);
        columns[i % targetColumns]->addWidget(cards[i]);
    }

    // Give every column a trailing stretch so cards pack to the top and unused columns stay collapsed.
    for (QVBoxLayout* col : columns)
    {
        col->addStretch(1);
    }
}

void ThemeEditorWidget::ReflowCards(bool force)
{
    ReflowColumns(m_scrollArea, m_columnContainer, m_columnLayouts, m_cards, m_currentColumns, force);
}

void ThemeEditorWidget::ReflowStructureCards(bool force)
{
    ReflowColumns(m_structureScroll, m_structureContainer, m_structureColumnLayouts, m_structureCards, m_structureCurrentColumns, force);
}

//////////////////////////////////////////////////////////////////////////
// resizeEvent -- trigger reflow when widget is resized
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    ReflowCards();
    ReflowStructureCards();
}

//////////////////////////////////////////////////////////////////////////
// showEvent -- reflow once the widget has its real (docked) width
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // At construction the widget has no meaningful width, so the initial reflow defaults
    // to a single column. Reflow once after the real size is known.
    QTimer::singleShot(0, this, [this]() { ReflowCards(); ReflowStructureCards(); });
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

    // Tear down old structure cards + any trailing stretch spacers in the structure columns.
    for (QFrame* card : m_structureCards)
    {
        card->setParent(nullptr);
        delete card;
    }
    m_structureCards.clear();
    for (QVBoxLayout* col : m_structureColumnLayouts)
    {
        while (QLayoutItem* item = col->takeAt(0))
        {
            delete item;
        }
    }
    m_structureCurrentColumns = 0; // Force a reflow after rebuild.

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
        m_structureCards.append(BuildCard(def, m_structureContainer));
    }

    // Distribute structure cards into columns (forced: card set just changed).
    ReflowStructureCards(true);

    // Re-apply any active search filter to the freshly rebuilt cards.
    if (m_searchBox && !m_searchBox->text().isEmpty())
    {
        FilterTokens(m_searchBox->text());
    }
}

//////////////////////////////////////////////////////////////////////////
// FilterTokens -- show/hide token rows (and empty cards) by name/value
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::FilterTokens(const QString& text)
{
    const QString needle = text.trimmed();
    const bool searching = !needle.isEmpty();

    auto filterCards = [&](const QList<QFrame*>& cards)
    {
        for (QFrame* card : cards)
        {
            QFormLayout* form = card->findChild<QFormLayout*>();
            if (!form)
            {
                continue;
            }
            QWidget*     body   = form->parentWidget();
            QToolButton* header = card->findChild<QToolButton*>();

            int visibleRows = 0;
            for (int row = 0; row < form->rowCount(); ++row)
            {
                QWidget* labelW = nullptr;
                QWidget* fieldW = nullptr;
                if (QLayoutItem* li = form->itemAt(row, QFormLayout::LabelRole))
                {
                    labelW = li->widget();
                }
                if (QLayoutItem* fi = form->itemAt(row, QFormLayout::FieldRole))
                {
                    fieldW = fi->widget();
                }

                bool match = !searching;
                if (searching)
                {
                    if (auto* lbl = qobject_cast<QLabel*>(labelW))
                    {
                        match = lbl->text().contains(needle, Qt::CaseInsensitive);
                    }
                    if (!match && fieldW)
                    {
                        // Also match the value text (e.g. search a hex like "FFFFFF").
                        if (auto* valueLabel = fieldW->findChild<QLabel*>())
                        {
                            match = valueLabel->text().contains(needle, Qt::CaseInsensitive);
                        }
                    }
                }

                if (labelW)
                {
                    labelW->setVisible(match);
                }
                if (fieldW)
                {
                    fieldW->setVisible(match);
                }
                if (match)
                {
                    ++visibleRows;
                }
            }

            // Hide a card entirely when nothing in it matches; expand it when it has matches.
            card->setVisible(visibleRows > 0);
            if (searching && visibleRows > 0 && body && header)
            {
                body->setVisible(true);
                header->setChecked(true);
                header->setArrowType(Qt::DownArrow);
            }
        }
    };

    filterCards(m_cards);
    filterCards(m_structureCards);
    // No reflow here: hidden (non-matching) cards take zero size in the column layouts, so the visible
    // matches pack on their own. Re-running the reflow would re-parent and re-show the filtered cards.
}

//////////////////////////////////////////////////////////////////////////
// OnApplyToEditor
//////////////////////////////////////////////////////////////////////////
void ThemeEditorWidget::OnApplyToEditor()
{
    AzQtComponents::StyleManager::reapplyTheme();

    // Persist the applied (still unnamed) edits so they survive an editor restart and so external
    // tools (the Class Wizard) match the editor. Tagged with the base theme; cleared on switch/reload.
    const QString theme = AzQtComponents::StyleManager::currentThemeName();
    ThemeWorkingOverrides::SaveSelectedTheme(theme);
    ThemeWorkingOverrides::Save(m_effectiveFlat, theme);
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

    // Rebuild the token cards for the newly active theme (repopulates m_effectiveFlat).
    RebuildFromActiveTheme();

    // Publish the switch to the shared store immediately so a relaunched Class Wizard follows it
    // without an editor restart. SaveSelectedTheme records the name; Save publishes the resolved
    // palette, so the wizard needs no theme files on its own --engine-path. This also overwrites any
    // prior Applied edits (selecting a new theme discards them).
    ThemeWorkingOverrides::SaveSelectedTheme(folderName);
    ThemeWorkingOverrides::Save(m_effectiveFlat, folderName);
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

    // Restoring the on-disk theme discards Applied edits -> drop the persisted working overlay too.
    ThemeWorkingOverrides::Clear();

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

    // The edits are now captured in the named theme file, so the unnamed working overlay is no
    // longer needed -- clear it so the named theme is the single source of truth.
    ThemeWorkingOverrides::Clear();

    // The new theme is now in the pool -- refresh the selector so it appears.
    PopulateThemeCombo();

    QMessageBox::information(this, tr("Save As New Theme"),
        tr("Theme \"%1\" saved.\n\nIt will appear in Global Preferences > General > Editor Theme "
           "the next time that combo is opened.").arg(safeName));
}
