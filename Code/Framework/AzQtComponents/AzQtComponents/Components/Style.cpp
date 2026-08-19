/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <QtGlobal>

#include <AzQtComponents/Components/Style.h>
#include <AzQtComponents/Components/StyleHelpers.h>
#include <AzQtComponents/Components/StyleManager.h>
#include <AzQtComponents/Components/ConfigHelpers.h>
#include <AzQtComponents/Components/Widgets/DialogButtonBox.h>
#include <AzQtComponents/Components/Widgets/DragAndDrop.h>
#include <AzQtComponents/Components/Widgets/PushButton.h>
#include <AzQtComponents/Components/Widgets/CheckBox.h>
#include <AzQtComponents/Components/Widgets/RadioButton.h>
#include <AzQtComponents/Components/Widgets/ProgressBar.h>
#include <AzQtComponents/Components/Widgets/Slider.h>
#include <AzQtComponents/Components/Widgets/Card.h>
#include <AzQtComponents/Components/Widgets/ColorPicker.h>
#include <AzQtComponents/Components/Widgets/Eyedropper.h>
#include <AzQtComponents/Components/Widgets/ColorPicker/PaletteView.h>
#include <AzQtComponents/Components/Widgets/LineEdit.h>
#include <AzQtComponents/Components/Widgets/ComboBox.h>
#include <AzQtComponents/Components/Widgets/BrowseEdit.h>
#include <AzQtComponents/Components/Widgets/BreadCrumbs.h>
#include <AzQtComponents/Components/Widgets/SpinBox.h>
#include <AzQtComponents/Components/Widgets/ScrollBar.h>
#include <AzQtComponents/Components/Widgets/StatusBar.h>
#include <AzQtComponents/Components/Widgets/TabWidget.h>
#include <AzQtComponents/Components/Widgets/TableView.h>
#include <AzQtComponents/Components/Widgets/TreeView.h>
#include <AzQtComponents/Components/Widgets/Menu.h>
#include <AzQtComponents/Components/Widgets/Text.h>
#include <AzQtComponents/Components/Widgets/ToolBar.h>
#include <AzQtComponents/Components/Widgets/ToolButton.h>
#include <AzQtComponents/Components/Widgets/VectorInput.h>
#include <AzQtComponents/Components/FilteredSearchWidget.h>
#include <AzQtComponents/Components/Widgets/AssetFolderThumbnailView.h>
#include <AzQtComponents/Components/Titlebar.h>
#include <AzQtComponents/Components/StyledBusyLabel.h>
#include <AzQtComponents/Utilities/TextUtilities.h>

AZ_PUSH_DISABLE_WARNING(4251, "-Wunknown-warning-option") // 4251: class '...' needs to have dll-interface to be used by clients of class '...'
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QLayout>
#include <QPainterPath>
#include <QTabBar>
#include <QToolBar>
#include <QComboBox>
#include <QDebug>
#include <QFile>
#include <QFileSystemWatcher>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListView>
#include <QObject>
#include <QPainter>
#include <QPixmapCache>
#include <QProgressBar>
#include <QSvgRenderer>
#include <QPushButton>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QSet>
#include <QSettings>
#include <QStyleOptionToolButton>
#include <QTableView>
#include <QTextEdit>
#include <QToolButton>
AZ_POP_DISABLE_WARNING

#include <limits>
#include <QListWidget>

namespace AzQtComponents
{
    // Add this css class to QTreeView's if you want to have absolute control of styling
    // in stylesheets. If you don't add this class, the expand arrows will ALWAYS paint.
    // See ::drawPrimitive below for more info.
    static QString g_treeViewDisableDefaultArrorPainting = QStringLiteral("DisableArrowPainting");

    static const char g_removeAllStylingProperty[] = {"RemoveAllStyling"};

    // Constant for the docking drop zone hotspot color when hovered over
    static const QColor g_dropZoneColorOnHover(23, 163, 205);


    // Private data structure
    struct Style::Data
    {
        QPalette palette;

        PushButton::Config pushButtonConfig;
        RadioButton::Config radioButtonConfig;
        CheckBox::Config checkBoxConfig;
        ProgressBar::Config progressBarConfig;
        Slider::Config sliderConfig;
        Card::Config cardConfig;
        ColorPicker::Config colorPickerConfig;
        Eyedropper::Config eyedropperConfig;
        PaletteView::Config paletteViewConfig;
        LineEdit::Config lineEditConfig;
        ComboBox::Config comboBoxConfig;
        BrowseEdit::Config browseEditConfig;
        BreadCrumbs::Config breadCrumbsConfig;
        SpinBox::Config spinBoxConfig;
        ScrollBar::Config scrollBarConfig;
        TabWidget::Config tabWidgetConfig;
        TableView::Config tableViewConfig;
        Text::Config textConfig;
        FilteredSearchWidget::Config filteredSearchWidgetConfig;
        AssetFolderThumbnailView::Config assetFolderThumbnailViewConfig;
        TitleBar::Config titleBarConfig;
        Menu::Config menuConfig;
        ToolButton::Config toolButtonConfig;
        DockBarButton::Config dockBarButtonConfig;
        StatusBar::Config statusBarConfig;
        DragAndDrop::Config dragAndDropConfig;
        ToolBar::Config toolBarConfig;
        TreeView::Config treeViewConfig;

        QFileSystemWatcher watcher;

        QSet<QObject*> widgetsToRepolishOnReload;
    };

    // Local template function to load config data from .ini files
    template <typename ConfigType, typename WidgetType>
    void loadConfig(Style* style, QFileSystemWatcher* watcher, ConfigType* config, const QString& path)
    {
        QString fullPath = QStringLiteral("AzQtComponentWidgets:%1").arg(path);
        ConfigHelpers::loadConfig<ConfigType, WidgetType>(watcher, config, fullPath, style, std::bind(&Style::settingsReloaded, style));
    }

    Style::DrawWidgetSentinel::DrawWidgetSentinel(const QWidget* widgetAboutToDraw)
        : m_style(qobject_cast<const Style*>(widgetAboutToDraw->style()))
        , m_lastDrawWidget(m_style ? m_style->m_drawControlWidget : nullptr)
    {
        if (m_style)
        {
            m_style->m_drawControlWidget = widgetAboutToDraw;
        }
    }

    Style::DrawWidgetSentinel::~DrawWidgetSentinel()
    {
        if (m_style)
        {
            m_style->m_drawControlWidget = m_lastDrawWidget.data();
        }
    }

    Style::Style(QStyle* style)
        : QProxyStyle(style)
        , m_data(new Style::Data)
    {
        SpinBox::initializeWatcher();
        LineEdit::initializeWatcher();
        ScrollBar::initializeWatcher();
        ComboBox::initializeWatcher();
        TreeView::initializeWatcher();

        // set up settings watchers
        loadConfig<PushButton::Config, PushButton>(this, &m_data->watcher, &m_data->pushButtonConfig, "PushButtonConfig.ini");
        loadConfig<RadioButton::Config, RadioButton>(this, &m_data->watcher, &m_data->radioButtonConfig, "RadioButtonConfig.ini");
        loadConfig<CheckBox::Config, CheckBox>(this, &m_data->watcher, &m_data->checkBoxConfig, "CheckBoxConfig.ini");
        loadConfig<ProgressBar::Config, ProgressBar>(this, &m_data->watcher, &m_data->progressBarConfig, "ProgressBarConfig.ini");
        loadConfig<Slider::Config, Slider>(this, &m_data->watcher, &m_data->sliderConfig, "SliderConfig.ini");
        loadConfig<Card::Config, Card>(this, &m_data->watcher, &m_data->cardConfig, "CardConfig.ini");
        loadConfig<ColorPicker::Config, ColorPicker>(this, &m_data->watcher, &m_data->colorPickerConfig, "ColorPickerConfig.ini");
        loadConfig<Eyedropper::Config, Eyedropper>(this, &m_data->watcher, &m_data->eyedropperConfig, "EyedropperConfig.ini");
        loadConfig<PaletteView::Config, PaletteView>(this, &m_data->watcher, &m_data->paletteViewConfig, "ColorPicker/PaletteViewConfig.ini");
        loadConfig<LineEdit::Config, LineEdit>(this, &m_data->watcher, &m_data->lineEditConfig, "LineEditConfig.ini");
        loadConfig<ComboBox::Config, ComboBox>(this, &m_data->watcher, &m_data->comboBoxConfig, "ComboBoxConfig.ini");
        loadConfig<BrowseEdit::Config, BrowseEdit>(this, &m_data->watcher, &m_data->browseEditConfig, "BrowseEditConfig.ini");
        loadConfig<BreadCrumbs::Config, BreadCrumbs>(this, &m_data->watcher, &m_data->breadCrumbsConfig, "BreadCrumbsConfig.ini");
        loadConfig<SpinBox::Config, SpinBox>(this, &m_data->watcher, &m_data->spinBoxConfig, "SpinBoxConfig.ini");
        loadConfig<ScrollBar::Config, ScrollBar>(this, &m_data->watcher, &m_data->scrollBarConfig, "ScrollBarConfig.ini");
        loadConfig<TabWidget::Config, TabWidget>(this, &m_data->watcher, &m_data->tabWidgetConfig, "TabWidgetConfig.ini");
        loadConfig<TableView::Config, TableView>(this, &m_data->watcher, &m_data->tableViewConfig, "TableViewConfig.ini");
        loadConfig<Text::Config, Text>(this, &m_data->watcher, &m_data->textConfig, "TextConfig.ini");
        loadConfig<FilteredSearchWidget::Config, FilteredSearchWidget>(this, &m_data->watcher, &m_data->filteredSearchWidgetConfig, "FilteredSearchWidgetConfig.ini");
        loadConfig<AssetFolderThumbnailView::Config, AssetFolderThumbnailView>(this, &m_data->watcher, &m_data->assetFolderThumbnailViewConfig, "AssetFolderThumbnailViewConfig.ini");
        loadConfig<TitleBar::Config, TitleBar>(this, &m_data->watcher, &m_data->titleBarConfig, "TitleBarConfig.ini");
        loadConfig<Menu::Config, Menu>(this, &m_data->watcher, &m_data->menuConfig, "MenuConfig.ini");
        loadConfig<ToolButton::Config, ToolButton>(this, &m_data->watcher, &m_data->toolButtonConfig, "ToolButtonConfig.ini");
        loadConfig<DockBarButton::Config, DockBarButton>(this, &m_data->watcher, &m_data->dockBarButtonConfig, "DockBarButtonConfig.ini");
        loadConfig<StatusBar::Config, StatusBar>(this, &m_data->watcher, &m_data->statusBarConfig, "StatusBarConfig.ini");
        loadConfig<DragAndDrop::Config, DragAndDrop>(this, &m_data->watcher, &m_data->dragAndDropConfig, "DragAndDropConfig.ini");
        loadConfig<ToolBar::Config, ToolBar>(this, &m_data->watcher, &m_data->toolBarConfig, "ToolBarConfig.ini");
        loadConfig<TreeView::Config, TreeView>(this, &m_data->watcher, &m_data->treeViewConfig, "TreeViewConfig.ini");

        VectorElement::initStaticVars(m_data->spinBoxConfig.labelSize);
        Slider::initStaticVars(m_data->sliderConfig.verticalToolTipOffset, m_data->sliderConfig.horizontalToolTipOffset);
    }

    Style::~Style()
    {
        SpinBox::uninitializeWatcher();
        LineEdit::uninitializeWatcher();
        ScrollBar::uninitializeWatcher();
        ComboBox::uninitializeWatcher();
        TreeView::uninitializeWatcher();
    }

    QIcon Style::icon(const QString& name)
    {
        const QString filePath = QStringLiteral(":/stylesheet/img/UI20/toolbar/%1.svg").arg(name);
        if (QFile::exists(filePath))
        {
            return QIcon(filePath);
        }

        qWarning() << "Style::icon: Couldn't find " << filePath;
        return {};
    }

    QColor Style::dropZoneColorOnHover()
    {
        return g_dropZoneColorOnHover;
    }


    QSize Style::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& size, const QWidget* widget) const
    {
        // SegmentBar buttons carry flagToIgnore (hasStyle == false) - size them before
        // the gate. SegmentControl.qss: height 28, padding 16/16, min-width 68.
        if (StyleManager::stylesheetsDisabled() && type == CT_PushButton && widget
            && (hasClass(widget, QStringLiteral("TabOne")) || hasClass(widget, QStringLiteral("TabFirst"))
                || hasClass(widget, QStringLiteral("TabMiddle")) || hasClass(widget, QStringLiteral("TabLast"))))
        {
            QSize segmentSize = QProxyStyle::sizeFromContents(type, option, size, widget);
            segmentSize.setHeight(28);
            segmentSize.setWidth(qMax(segmentSize.width() + 16, 70));
            return segmentSize;
        }

        if (!hasStyle(widget))
        {
            return QProxyStyle::sizeFromContents(type, option, size, widget);
        }

        if (StyleManager::stylesheetsDisabled())
        {
            switch (type)
            {
                case CT_ItemViewItem:
                {
                    // qss-era item view row floor (24px)
                    QSize itemSize = QProxyStyle::sizeFromContents(type, option, size, widget);
                    itemSize.setHeight(qMax(itemSize.height(), 24));
                    return itemSize;
                }
                case CT_TabBarTab:
                {
                    // TabWidgetConfig TabHeight 30; qss max tab width 200. DockTabBar excluded.
                    if (!(widget && widget->inherits("AzQtComponents::DockTabBar")))
                    {
                        QSize tabSize = QProxyStyle::sizeFromContents(type, option, size, widget);
                        tabSize.setHeight(30);
                        tabSize.setWidth(qMin(tabSize.width(), 200));
                        return tabSize;
                    }
                    break;
                }
                case CT_ComboBox:
                {
                    // qss-era combo height (16px content + border/padding = 20)
                    QSize comboSize = QProxyStyle::sizeFromContents(type, option, size, widget);
                    comboSize.setHeight(20);
                    return comboSize;
                }
                default:
                    break;
            }
        }

        switch (type)
        {
            case QStyle::CT_PushButton:
                if (qobject_cast<const QPushButton*>(widget))
                {
                    return PushButton::sizeFromContents(this, type, option, size, widget, m_data->pushButtonConfig);
                }
                break;

            case QStyle::CT_ToolButton:
                if (qobject_cast<const QToolButton*>(widget))
                {
                    return ToolButton::sizeFromContents(this, type, option, size, widget, m_data->toolButtonConfig);
                }
                break;

            case QStyle::CT_CheckBox:
                if (qobject_cast<const QCheckBox*>(widget))
                {
                    return CheckBox::sizeFromContents(this, type, option, size, widget, m_data->checkBoxConfig);
                }
                break;

            case QStyle::CT_RadioButton:
                if (qobject_cast<const QRadioButton*>(widget))
                {
                    return RadioButton::sizeFromContents(this, type, option, size, widget, m_data->radioButtonConfig);
                }
                break;

            case QStyle::CT_ProgressBar:
                if (qobject_cast<const QProgressBar*>(widget))
                {
                    return ProgressBar::sizeFromContents(this, type, option, size, widget, m_data->progressBarConfig);
                }
                break;
            case QStyle::CT_ComboBox:
            {
                if (qobject_cast<const QComboBox*>(widget))
                {
                    return ComboBox::sizeFromContents(this, type, option, size, widget, m_data->comboBoxConfig);
                }
                break;
            }
            case QStyle::CT_HeaderSection:
            {
                const auto headerSize = TableView::sizeFromContents(this, type, option, size, widget, m_data->tableViewConfig);
                if (headerSize.isValid())
                {
                    return headerSize;
                }
                break;
            }
        }

        return QProxyStyle::sizeFromContents(type, option, size, widget);
    }

    void Style::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
    {
        QScopedValueRollback<const QWidget*> rollbackDrawControl(m_drawControlWidget, widget);

        // SegmentBar buttons carry flagToIgnore (hasStyle == false), so their flattened
        // look must be handled BEFORE the gate.
        if (StyleManager::stylesheetsDisabled() && element == CE_PushButtonBevel && widget
            && (hasClass(widget, QStringLiteral("TabOne")) || hasClass(widget, QStringLiteral("TabFirst"))
                || hasClass(widget, QStringLiteral("TabMiddle")) || hasClass(widget, QStringLiteral("TabLast"))))
        {
            // SegmentControl.qss: flat #333333, hover #444444, selected #555555, #222222 border
            QColor segmentFill(0x33, 0x33, 0x33);
            if (option->state.testFlag(QStyle::State_On) || option->state.testFlag(QStyle::State_Sunken))
            {
                segmentFill = QColor(0x55, 0x55, 0x55);
            }
            else if (option->state.testFlag(QStyle::State_MouseOver))
            {
                segmentFill = QColor(0x44, 0x44, 0x44);
            }
            // SegmentControl.qss: seamless bar - each button has a 1px #222222 border
            // and non-first buttons carry margin-left:-1px so adjacent borders OVERLAP
            // into a single line. Mirror that by extending the border rect 1px left
            // for every non-leading segment.
            QRect borderRect = option->rect;
            const bool leadingSegment = hasClass(widget, QStringLiteral("TabOne")) || hasClass(widget, QStringLiteral("TabFirst"));
            if (!leadingSegment)
            {
                borderRect.adjust(-1, 0, 0, 0);
            }

            painter->fillRect(option->rect, segmentFill);
            painter->setPen(QColor(0x22, 0x22, 0x22));
            painter->drawRect(borderRect.adjusted(0, 0, -1, -1));
            return;
        }

        if (!hasStyle(widget))
        {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }

        prepPainter(painter);
        switch (element)
        {
            case CE_ShapedFrame:
            {
                // FilteredSearchWidget.qss tag chips: dark blocks rgb(46,46,46),
                // 1px #808080 border, 2px radius
                if (StyleManager::stylesheetsDisabled() && widget
                    && (widget->inherits("AzQtComponents::FilterCriteriaButton")
                        || widget->inherits("AzQtComponents::FilterTextButton")))
                {
                    painter->save();
                    painter->setRenderHint(QPainter::Antialiasing);
                    painter->setPen(QColor(0x80, 0x80, 0x80));
                    painter->setBrush(QColor(46, 46, 46));
                    painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);
                    painter->restore();
                    return;
                }
                if (BrowseEdit::drawFrame(this, option, painter, widget, m_data->browseEditConfig))
                {
                    return;
                }
            }
            break;

            case CE_Splitter:
            {
                // Splitter.qss: handle #222222, hover #1E70EB
                if (StyleManager::stylesheetsDisabled())
                {
                    painter->fillRect(option->rect,
                        option->state.testFlag(QStyle::State_MouseOver) ? QColor(0x1E, 0x70, 0xEB) : QColor(0x22, 0x22, 0x22));
                    return;
                }
            }
            break;

            case CE_TabBarTabShape:
            {
                // TabWidget.qss: flat boxes, inactive #333333 / active #444444, #111111 border
                if (StyleManager::stylesheetsDisabled() && !(widget && widget->inherits("AzQtComponents::DockTabBar")))
                {
                    const bool selected = option->state.testFlag(QStyle::State_Selected);
                    painter->fillRect(option->rect, selected ? QColor(0x44, 0x44, 0x44) : QColor(0x33, 0x33, 0x33));
                    painter->setPen(QColor(0x11, 0x11, 0x11));
                    painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
                    return;
                }
            }
            break;

            case CE_DockWidgetTitle:
            {
                // QDockWidget.qss backup path: title strip #333333 (raw QDockWidgets in gems)
                if (StyleManager::stylesheetsDisabled())
                {
                    painter->fillRect(option->rect, QColor(0x33, 0x33, 0x33));
                    if (auto dockOption = qstyleoption_cast<const QStyleOptionDockWidget*>(option))
                    {
                        painter->setPen(QColor(Qt::white));
                        painter->drawText(option->rect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, dockOption->title);
                    }
                    return;
                }
            }
            break;

            case CE_PushButtonBevel:
            {
                if (qobject_cast<const QPushButton*>(widget))
                {
                    if (PushButton::drawPushButtonBevel(this, option, painter, widget, m_data->pushButtonConfig))
                    {
                        return;
                    }
                }
            }
            break;

            case CE_CheckBox:
            {
                if (qobject_cast<const QCheckBox*>(widget))
                {
                    if (CheckBox::drawCheckBox(this, option, painter, widget, m_data->checkBoxConfig))
                    {
                        return;
                    }
                }
            }
            break;

            case CE_CheckBoxLabel:
            {
                if (qobject_cast<const QCheckBox*>(widget))
                {
                    if (CheckBox::drawCheckBoxLabel(this, option, painter, widget, m_data->checkBoxConfig))
                    {
                        return;
                    }
                }
            }
            break;

            case CE_RadioButton:
            {
                if (qobject_cast<const QRadioButton*>(widget))
                {
                    if (RadioButton::drawRadioButton(this, option, painter, widget, m_data->radioButtonConfig))
                    {
                        return;
                    }
                }
            }
            break;

            case CE_RadioButtonLabel:
            {
                if (qobject_cast<const QRadioButton*>(widget))
                {
                    if (RadioButton::drawRadioButtonLabel(this, option, painter, widget, m_data->radioButtonConfig))
                    {
                        return;
                    }
                }
            }
            break;

            case CE_Header:
            {
                // Flatten: hard-fill header sections (TableView.qss #2d2d2d) - the
                // palette-only route was unreliable because Fusion blends Button
                if (StyleManager::stylesheetsDisabled() && qobject_cast<const QHeaderView*>(widget))
                {
                    painter->fillRect(option->rect, QColor(0x2D, 0x2D, 0x2D));
                    painter->setPen(QColor(0x22, 0x22, 0x22));
                    painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
                    painter->drawLine(option->rect.topRight(), option->rect.bottomRight());
                    if (auto headerOption = qstyleoption_cast<const QStyleOptionHeader*>(option))
                    {
                        QStyleOptionHeader labelOption = *headerOption;
                        labelOption.rect = option->rect.adjusted(7, 0, -4, 0); // qss header pad-left 7
                        labelOption.palette.setColor(QPalette::ButtonText, QColor(0xCC, 0xCC, 0xCC));
                        QProxyStyle::drawControl(CE_HeaderLabel, &labelOption, painter, widget);
                    }
                    return;
                }
                if (qobject_cast<const QHeaderView*>(widget))
                {
                    if (TableView::drawHeader(this, option, painter, widget, m_data->tableViewConfig))
                    {
                        return;
                    }
                }
            }
            break;

            case CE_ComboBoxLabel:
            {
                if (qobject_cast<const QComboBox*>(widget))
                {
                    if (ComboBox::drawComboBoxLabel(this, option, painter, widget, m_data->comboBoxConfig))
                    {
                        return;
                    }
                }
            }
            break;

            case CE_ItemViewItem:
            {
                // For styling QTableView and QListView
                auto tableView = qobject_cast<const QTableView*>(widget);
                auto listView = qobject_cast<const QListView*>(widget);
                auto validListView = listView && !StyleHelpers::findParent<QComboBox>(listView);
                // For styling QTreeView (but not AzQtComponents::TableView)
                auto treeView = qobject_cast<const QTreeView*>(widget);
                auto validTreeView = treeView && !qobject_cast<const TableView*>(widget);

                auto itemOption = qstyleoption_cast<const QStyleOptionViewItem*>(option);

                if ((tableView || validListView) && itemOption)
                {
                    auto copy = *itemOption;
                    copy.styleObject = const_cast<QObject*>(qobject_cast<const QObject*>(widget));

                    // Need to stretch the focus rect to span all rows in a QTableView.
                    // Taking into account also possible headers.
                    if (tableView)
                    {
                        auto hHdr = tableView->horizontalHeader()->isVisible() ? tableView->horizontalHeader()->height() : 0;
                        auto vHdr = tableView->verticalHeader()->isVisible() ? tableView->verticalHeader()->width() : 0;

                        auto rowRect = copy.rect;
                        rowRect.setWidth(tableView->width());
                        rowRect.moveLeft(vHdr);
                        rowRect.adjust(0, hHdr, 0, hHdr);

                        copy.state.setFlag(QStyle::State_MouseOver, rowRect.contains(tableView->mapFromGlobal(QCursor::pos())));

                        // Draw focus frame rectangle in all cells belonging to the selected row.
                        // The focus frame rectangle is only drawn if QStyle::State_HasFocus is set,
                        // but we only get QStyle::State_Selected from the selection model, so we
                        // need to force QStyle::State_HasFocus whenever QStyle::State_Selected is set.
                        copy.state.setFlag(QStyle::State_HasFocus, tableView->hasFocus() && (copy.state & QStyle::State_Selected));
                    }

                    // QStyleSheetStyle seems to ignore the background color set by the model
                    painter->fillRect(copy.rect, copy.backgroundBrush);

                    return QProxyStyle::drawControl(element, &copy, painter, widget);
                }
                else if (validTreeView)
                {
                    if (TreeView::isBranchLinesEnabled(treeView) && qobject_cast<BranchDelegate*>(treeView->itemDelegate()) && option->state.testFlag(QStyle::State_Children))
                    {
                        auto copy = *itemOption;
                        copy.rect.adjust(treeView->indentation(), 0, 0, 0);
                        return QProxyStyle::drawControl(element, &copy, painter, widget);
                    }
                }
            }
            break;
        }

        return QProxyStyle::drawControl(element, option, painter, widget);
    }

    //////////////////////////////////////////////////////////////////////////
    // Flattened indicator painting (GUI flattening)
    //
    // Check box, toggle switch, expander and radio button indicators were
    // delivered by QSS "image:" rules (CheckBox.qss / RadioButton.qss) over
    // SVGs compiled into the AzQtComponents resources. In flattened mode the
    // style owns them: widget state maps to the same SVG assets, rendered
    // through the pixmap cache. Item view check indicators route through the
    // same PE_IndicatorCheckBox primitive.
    //////////////////////////////////////////////////////////////////////////
    static QPixmap flattenedIndicatorPixmap(const QString& svgPath, const QSize& size, qreal devicePixelRatio)
    {
        const QString cacheKey = QString::fromLatin1("O3DEFlatInd|%1|%2x%3|%4")
            .arg(svgPath).arg(size.width()).arg(size.height()).arg(devicePixelRatio);

        QPixmap pixmap;
        if (!QPixmapCache::find(cacheKey, &pixmap))
        {
            QSvgRenderer renderer(svgPath);
            pixmap = QPixmap(size * devicePixelRatio);
            pixmap.setDevicePixelRatio(devicePixelRatio);
            pixmap.fill(Qt::transparent);
            QPainter pixmapPainter(&pixmap);
            // Explicit logical bounds: render(painter) alone uses the pixmap's DEVICE
            // pixel size as logical bounds, drawing scale-factor times too large on
            // high-DPI displays (the clipped-indicator bug).
            renderer.render(&pixmapPainter, QRectF(QPointF(0.0, 0.0), QSizeF(size)));
            pixmapPainter.end();
            QPixmapCache::insert(cacheKey, pixmap);
        }
        return pixmap;
    }

    static void drawFlattenedIndicator(const QString& svgPath, QPainter* painter, const QRect& rect)
    {
        if (svgPath.isEmpty() || !rect.isValid())
        {
            return;
        }

        const qreal devicePixelRatio = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
        painter->drawPixmap(rect, flattenedIndicatorPixmap(svgPath, rect.size(), devicePixelRatio));
    }

    static QString flattenedCheckBoxIndicatorPath(const QStyleOption* option, const QWidget* widget)
    {
        const bool enabled = option->state.testFlag(QStyle::State_Enabled);
        const bool focused = option->state.testFlag(QStyle::State_HasFocus);
        const bool checked = option->state.testFlag(QStyle::State_On);
        const bool partial = option->state.testFlag(QStyle::State_NoChange);

        // QCheckBox variants carried as style classes (see CheckBox.qss)
        if (widget && Style::hasClass(widget, QStringLiteral("ToggleSwitch")))
        {
            QString name = checked ? QStringLiteral("checked") : QStringLiteral("unchecked");
            if (!enabled)
            {
                name += QStringLiteral("-disabled");
            }
            else if (focused)
            {
                name += QStringLiteral("-focus");
            }
            return QStringLiteral(":/stylesheet/img/UI20/toggleswitch/%1.svg").arg(name);
        }

        if (widget && Style::hasClass(widget, QStringLiteral("Expander")))
        {
            QString name = checked ? QStringLiteral("caret-down") : QStringLiteral("caret-right");
            if (!enabled)
            {
                name += QStringLiteral("-disabled");
            }
            return QStringLiteral(":/Cards/img/UI20/Cards/%1.svg").arg(name);
        }

        QString name = partial ? QStringLiteral("partial-selected") : (checked ? QStringLiteral("on") : QStringLiteral("off"));
        if (!enabled)
        {
            name += QStringLiteral("-disabled");
        }
        else if (focused)
        {
            name += QStringLiteral("-focus");
        }
        return QStringLiteral(":/stylesheet/img/UI20/checkbox/%1.svg").arg(name);
    }

    static QString flattenedRadioButtonIndicatorPath(const QStyleOption* option)
    {
        const bool enabled = option->state.testFlag(QStyle::State_Enabled);
        const bool focused = option->state.testFlag(QStyle::State_HasFocus);

        QString name = option->state.testFlag(QStyle::State_On) ? QStringLiteral("checked") : QStringLiteral("unchecked");
        if (!enabled)
        {
            name += QStringLiteral("-disabled");
        }
        else if (focused)
        {
            name += QStringLiteral("-focus");
        }
        return QStringLiteral(":/stylesheet/img/UI20/radiobutton/%1.svg").arg(name);
    }

    //////////////////////////////////////////////////////////////////////////
    // Flattened scroll bar painting (GUI flattening)
    //
    // ScrollBar.qss: transparent 8px lane, rounded semi-transparent handle
    // (rgba 255,255,255,40%; DarkScrollBar class rgba 136,136,136,64%),
    // opaque lane fill on hover (#555555 light / #DCDCDC dark), arrow
    // buttons removed. The DarkScrollBar class lives on the owning
    // QAbstractScrollArea (Console, Python terminal).
    //////////////////////////////////////////////////////////////////////////
    static void drawFlattenedScrollBar(const Style* style, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget)
    {
        auto sliderOption = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (!sliderOption)
        {
            return;
        }

        const QWidget* scrollArea = widget ? widget->parentWidget() : nullptr;
        while (scrollArea && !qobject_cast<const QAbstractScrollArea*>(scrollArea))
        {
            scrollArea = scrollArea->parentWidget();
        }
        const bool dark = scrollArea && Style::hasClass(scrollArea, QStringLiteral("DarkScrollBar"));

        const QColor hoverFill = dark ? QColor(0xDC, 0xDC, 0xDC) : QColor(0x55, 0x55, 0x55);
        const QColor handleColor = dark ? QColor(136, 136, 136, 163) : QColor(255, 255, 255, 102);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        if (option->state & QStyle::State_MouseOver)
        {
            painter->fillRect(option->rect, hoverFill);
        }

        QRect handleRect = style->subControlRect(QStyle::CC_ScrollBar, option, QStyle::SC_ScrollBarSlider, widget);
        if (handleRect.isValid())
        {
            const bool horizontal = sliderOption->orientation == Qt::Horizontal;
            handleRect.adjust(horizontal ? 0 : 2, horizontal ? 2 : 0, horizontal ? 0 : -2, horizontal ? -2 : 0);
            painter->setPen(Qt::NoPen);
            painter->setBrush(handleColor);
            painter->drawRoundedRect(handleRect, 2, 2);
        }

        painter->restore();
    }

    void Style::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
    {
        if (element == PE_IndicatorDockWidgetResizeHandle)
        {
            // There is a bug in Qt where the option state Horizontal flag is
            // being set/unset incorrectly for some cases, particularly when you
            // have multiple dock widgets docked on the absolute edges, so we
            // can rely instead on the width/height relationship to determine
            // if the resize handle should be horizontal or vertical.

            // Here we just patch the QStyleOption and forward the drawing to its normal route
            if (auto optionHacked = const_cast<QStyleOption*>(option))
            {
                const bool isHorizontal = option->rect.width() > option->rect.height();
                // TODO for Qt >= 5.7: Simply replace with: optionHacked->state.setFlag(QStyle::State_Horizontal, isHorizontal);
                if (isHorizontal)
                {
                    optionHacked->state |= QStyle::State_Horizontal;
                }
                else
                {
                    optionHacked->state &= ~QStyle::State_Horizontal;
                }
            }

            return QProxyStyle::drawPrimitive(element, option, painter, widget);
        }

        if (!hasStyle(widget))
        {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        prepPainter(painter);
        switch (element)
        {
            case PE_PanelLineEdit:
            {
                if (LineEdit::drawFrame(this, option, painter, widget, m_data->lineEditConfig))
                {
                    return;
                }
            }
            break;

            case PE_FrameFocusRect:
            {
                // We're not passing the widget parameter to TableView because for some reason QTreeView doesn't
                // use this parameter when calling QStyle::drawPrimitive to draw PE_FrameFocusRect (so it's always nullptr)
                if (TableView::drawFrameFocusRect(this, option, painter, m_data->tableViewConfig))
                {
                    return;
                }
                else if (qobject_cast<const QPushButton*>(widget) || qobject_cast<const QToolButton*>(widget))
                {
                    if (PushButton::drawPushButtonFocusRect(this, option, painter, widget, m_data->pushButtonConfig))
                    {
                        return;
                    }
                }
            }
            break;

            case PE_PanelButtonTool:
            {
                if (PushButton::drawPushButtonBevel(this, option, painter, widget, m_data->pushButtonConfig))
                {
                    return;
                }
            }
            break;

            case PE_IndicatorArrowDown:
            {
                if (qobject_cast<const QComboBox*>(widget) && ComboBox::drawIndicatorArrow(this, option, painter, widget, m_data->comboBoxConfig))
                {
                    return;
                }
                else if (PushButton::drawIndicatorArrowDown(this, option, painter, widget, m_data->pushButtonConfig))
                {
                    return;
                }
                else if (ToolButton::drawIndicatorArrowDown(this, option, painter, widget, m_data->toolButtonConfig))
                {
                    return;
                }
            }
            break;

            case PE_IndicatorItemViewItemDrop:
            {
                if (PaletteView::drawDropIndicator(this, option, painter, widget, m_data->paletteViewConfig))
                {
                    return;
                }
                else if (DragAndDrop::drawDropIndicator(this, option, painter, widget, m_data->dragAndDropConfig))
                {
                    return;
                }
            }
            break;

            case PE_IndicatorBranch:
            {
                // Flatten FIRST: the BranchDelegate path below delegates to raw Fusion
                // baseStyle() (massive native arrows); in flatten mode the caret SVGs
                // own this for every tree. TableView hides branches (qss).
                if (StyleManager::stylesheetsDisabled() && !qobject_cast<const TableView*>(widget))
                {
                    if (option->state.testFlag(QStyle::State_Children))
                    {
                        // Natural caret svg sizes (TableView.qss draws them unscaled:
                        // closed 4x8, open 8x4)
                        const bool open = option->state.testFlag(QStyle::State_Open);
                        QRect glyphRect = open ? QRect(0, 0, 8, 4) : QRect(0, 0, 4, 8);
                        glyphRect.moveCenter(option->rect.center());
                        drawFlattenedIndicator(open ? QStringLiteral(":/TreeView/open.svg")
                                                    : QStringLiteral(":/TreeView/closed.svg"),
                            painter, glyphRect);
                    }
                    return;
                }
                if (TreeView::drawBranchIndicator(this, option, painter, widget, m_data->treeViewConfig))
                {
                    return;
                }
            }
            break;

            case PE_IndicatorItemViewItemCheck:
            {
                if (StyleManager::stylesheetsDisabled())
                {
                    // Combo box popups have NO check indicators (upstream combo design:
                    // current row is pre-highlighted instead)
                    for (const QWidget* ancestor = widget; ancestor; ancestor = ancestor->parentWidget())
                    {
                        if (ancestor->inherits("QComboBoxPrivateContainer"))
                        {
                            return;
                        }
                    }
                    // Item view check boxes share the CheckBox.qss indicator images
                    drawFlattenedIndicator(flattenedCheckBoxIndicatorPath(option, widget), painter, option->rect);
                    return;
                }
            }
            break;

            case PE_IndicatorTabClose:
            {
                if (StyleManager::stylesheetsDisabled())
                {
                    if (option->state.testFlag(QStyle::State_MouseOver))
                    {
                        painter->fillRect(option->rect, QColor(0x44, 0x44, 0x44));
                    }
                    QRect glyphRect(0, 0, 12, 12);
                    glyphRect.moveCenter(option->rect.center());
                    drawFlattenedIndicator(QStringLiteral(":/Application/titlebar-close.svg"), painter, glyphRect);
                    return;
                }
            }
            break;

            case PE_FrameDockWidget:
            {
                // StyledDockWidget.qss: no frame when docked; 1px black when floating
                if (StyleManager::stylesheetsDisabled())
                {
                    const QDockWidget* dockWidget = qobject_cast<const QDockWidget*>(widget);
                    if (dockWidget && dockWidget->isFloating())
                    {
                        painter->setPen(QColor(Qt::black));
                        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
                    }
                    return;
                }
            }
            break;

            case PE_IndicatorCheckBox:
            {
                if (StyleManager::stylesheetsDisabled())
                {
                    drawFlattenedIndicator(flattenedCheckBoxIndicatorPath(option, widget), painter, option->rect);
                    return;
                }
            }
            break;

            case PE_IndicatorRadioButton:
            {
                if (StyleManager::stylesheetsDisabled())
                {
                    // The focus variant carries its own 18px ring; grow the 16px rect so the ring is not cropped
                    QRect indicatorRect = option->rect;
                    if (option->state.testFlag(QStyle::State_HasFocus) && option->state.testFlag(QStyle::State_Enabled))
                    {
                        indicatorRect.adjust(-1, -1, 1, 1);
                    }
                    drawFlattenedIndicator(flattenedRadioButtonIndicatorPath(option), painter, indicatorRect);
                    return;
                }
            }
            break;

            case PE_PanelStatusBar:
            {
                if (StatusBar::drawPanelStatusBar(this, option, painter, widget, m_data->statusBarConfig))
                {
                    return;
                }
            }
            break;

            case PE_PanelItemViewRow:
            {
                /** HACK
                 * For TableView, we want the first row to use the alternate color, not the second one.
                 * The "right" way to do this would be to invert the `background-color` and
                 * `alternate-background-color` properties in the stylesheet, but if we do this:
                 *
                 * - the widget won't inherit the background color from the base stylesheet;
                 * - the widget background outside the list will be filled with a light shade of gray instead
                 *   of the global background color, which is not what we want.
                 *
                 * So here we just flip the Alternate flag before letting the base style fill the item background.
                 **/
                if (auto* tableView = qobject_cast<const TableView*>(widget))
                {
                    if (tableView->alternatingRowColors())
                    {
                        if (auto itemOption = qstyleoption_cast<const QStyleOptionViewItem*>(option))
                        {
                            auto copy = *itemOption;
                            copy.features ^= QStyleOptionViewItem::Alternate;
                            return QProxyStyle::drawPrimitive(element, &copy, painter, widget);
                        }
                    }
                }
            }
            break;
        }

        return QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void Style::drawComplexControl(QStyle::ComplexControl element, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
    {
        if (!hasStyle(widget))
        {
            QProxyStyle::drawComplexControl(element, option, painter, widget);
            return;
        }

        prepPainter(painter);

        switch (element)
        {
            case CC_SpinBox:
                if (SpinBox::drawSpinBox(this, option, painter, widget, m_data->spinBoxConfig))
                {
                    return;
                }
                break;

            case CC_Slider:
                if (auto sliderOption = qstyleoption_cast<const QStyleOptionSlider*>(option))
                {
                    if (Slider::drawSlider(this, sliderOption, painter, widget, m_data->sliderConfig))
                    {
                        return;
                    }
                }
                break;

            case CC_ToolButton:
                if (DockBarButton::drawDockBarButton(this, option, painter, widget, m_data->dockBarButtonConfig))
                {
                    return;
                }
                if (ToolButton::drawToolButton(this, option, painter, widget, m_data->toolButtonConfig))
                {
                    return;
                }
                break;

            case CC_ComboBox:
                // Flatten: flat rounded light face (BaseStyleSheet input rule shape);
                // framed combos otherwise fall to Fusion's native 3D bevel because the
                // ComboBox.cpp hooks decline when opt->frame is true.
                if (StyleManager::stylesheetsDisabled())
                {
                    painter->save();
                    painter->setRenderHint(QPainter::Antialiasing);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(option->palette.button());
                    painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);

                    if (auto comboOption = qstyleoption_cast<const QStyleOptionComboBox*>(option))
                    {
                        QStyleOption arrowOption = *option;
                        arrowOption.rect = subControlRect(CC_ComboBox, comboOption, SC_ComboBoxArrow, widget);
                        arrowOption.palette.setColor(QPalette::ButtonText, QColor(0x33, 0x33, 0x33));
                        QProxyStyle::drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);
                    }
                    painter->restore();
                    return;
                }
                if (ComboBox::drawComboBox(this, option, painter, widget, m_data->comboBoxConfig))
                {
                    return;
                }
                break;

            case CC_ScrollBar:
                if (StyleManager::stylesheetsDisabled())
                {
                    drawFlattenedScrollBar(this, option, painter, widget);
                    return;
                }
                if (ScrollBar::drawScrollBar(this, option, painter, widget, m_data->scrollBarConfig))
                {
                    return;
                }
                break;
        }

        return QProxyStyle::drawComplexControl(element, option, painter, widget);
    }

    void Style::drawDragIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
    {
        DragAndDrop::drawDragIndicator(this, option, painter, widget, m_data->dragAndDropConfig);
    }

    QPixmap Style::generatedIconPixmap(QIcon::Mode iconMode, const QPixmap& pixmap, const QStyleOption* option) const
    {
        if ((qobject_cast<const TableView*>(m_drawControlWidget) ||
            qobject_cast<const QListWidget*>(m_drawControlWidget)) && iconMode == QIcon::Mode::Selected)
        {
            return QProxyStyle::generatedIconPixmap(QIcon::Mode::Active, pixmap, option);
        }

        QPixmap generatedPixmap;
        generatedPixmap = Card::generatedIconPixmap(iconMode, pixmap, option, m_drawControlWidget, m_data->cardConfig);
        if (!generatedPixmap.isNull())
        {
            return generatedPixmap;
        }

        generatedPixmap = PushButton::generatedIconPixmap(iconMode, pixmap, option, m_data->pushButtonConfig);
        if (!generatedPixmap.isNull())
        {
            return generatedPixmap;
        }

        return QProxyStyle::generatedIconPixmap(iconMode, pixmap, option);
    }

    QRect Style::subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl subControl, const QWidget* widget) const
    {
        if (!hasStyle(widget))
        {
            return QProxyStyle::subControlRect(control, option, subControl, widget);
        }

        switch (control)
        {
            case CC_ComboBox:
            {
                if (auto comboBoxOption = qstyleoption_cast<const QStyleOptionComboBox*>(option))
                {
                    switch (subControl)
                    {
                    case SC_ComboBoxListBoxPopup:
                    {
                        QRect r = ComboBox::comboBoxListBoxPopupRect(this, comboBoxOption, widget, m_data->comboBoxConfig);
                        if (!r.isNull())
                        {
                            return r;
                        }
                    }
                    break;

                    default:
                        break;
                    }
                }
            }
            break;
            case CC_ScrollBar:
            {
                if (StyleManager::stylesheetsDisabled())
                {
                    if (auto sliderOption = qstyleoption_cast<const QStyleOptionSlider*>(option))
                    {
                        switch (subControl)
                        {
                            case SC_ScrollBarAddLine:
                            case SC_ScrollBarSubLine:
                                // ScrollBar.qss removes both arrow buttons (0px)
                                return QRect();

                            case SC_ScrollBarGroove:
                                return option->rect;

                            case SC_ScrollBarSlider:
                            {
                                const bool horizontal = sliderOption->orientation == Qt::Horizontal;
                                const int trackLength = horizontal ? option->rect.width() : option->rect.height();
                                const int sliderMin = 32; // matches PM_ScrollBarSliderMin above
                                const int range = sliderOption->maximum - sliderOption->minimum;
                                int sliderLength = range <= 0
                                    ? trackLength
                                    : (sliderOption->pageStep * trackLength) / (range + sliderOption->pageStep);
                                sliderLength = qBound(sliderMin, sliderLength, trackLength);
                                const int sliderPos = QStyle::sliderPositionFromValue(
                                    sliderOption->minimum, sliderOption->maximum, sliderOption->sliderPosition,
                                    trackLength - sliderLength, sliderOption->upsideDown);
                                return horizontal
                                    ? QRect(option->rect.x() + sliderPos, option->rect.y(), sliderLength, option->rect.height())
                                    : QRect(option->rect.x(), option->rect.y() + sliderPos, option->rect.width(), sliderLength);
                            }

                            default:
                                break;
                        }
                    }
                }
            }
            break;

            case CC_Slider:
            {
                if (auto sliderOption = qstyleoption_cast<const QStyleOptionSlider*>(option))
                {
                    switch (subControl)
                    {
                        case SC_SliderHandle:
                        {
                            QRect r = Slider::sliderHandleRect(this, sliderOption, widget, m_data->sliderConfig);
                            if (!r.isNull())
                            {
                                return r;
                            }
                        }
                        break;

                        case SC_SliderGroove:
                        {
                            QRect r = Slider::sliderGrooveRect(this, sliderOption, widget, m_data->sliderConfig);
                            if (!r.isNull())
                            {
                                return r;
                            }
                        }
                        break;

                        default:
                            break;
                    }
                }
            }
            break;

            case CC_SpinBox:
            {
                switch (subControl)
                {
                    case SC_SpinBoxEditField:
                    {
                        QRect r;
                        auto spinBox = qobject_cast<const SpinBox*>(widget);
                        auto doubleSpinBox = qobject_cast<const DoubleSpinBox*>(widget);
                        auto vectorElement = qobject_cast<const VectorElement*>(widget->parent());

                        if (vectorElement && doubleSpinBox)
                        {
                            r = VectorElement::editFieldRect(this, option, widget, m_data->spinBoxConfig);
                        }
                        else if (spinBox || doubleSpinBox)
                        {
                            r = SpinBox::editFieldRect(this, option, widget, m_data->spinBoxConfig);
                        }

                        if (!r.isNull())
                        {
                            return r;
                        }
                    }
                    break;

                    default:
                        break;
                }
            }
            break;

            case CC_ToolButton:
            {
                return ToolButton::subControlRect(this, option, subControl, widget, m_data->toolButtonConfig);
            }
        }

        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }

    QRect Style::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
    {
        if (!hasStyle(widget))
        {
            return QProxyStyle::subElementRect(element, option, widget);
        }

        switch (element)
        {
            case SE_ItemViewItemText:       // intentional fall-through
            case SE_ItemViewItemDecoration: // intentional fall-through
            case SE_ItemViewItemFocusRect:
            {
                auto optionItemView = qstyleoption_cast<const QStyleOptionViewItem*>(option);
                if (qobject_cast<const TableView*>(widget) && optionItemView)
                {
                    QRect r = TableView::itemViewItemRect(this, element, optionItemView, widget, m_data->tableViewConfig);
                    if (!r.isNull())
                    {
                        return r;
                    }
                }
            }
            break;
            case SE_LineEditContents:
            {
                QRect r = LineEdit::lineEditContentsRect(this, element, option, widget, m_data->lineEditConfig);
                if (!r.isNull())
                {
                    return r;
                }
            }
            break;
            case SE_TreeViewDisclosureItem:
            {
                auto treeView = static_cast<const QTreeView*>(widget);
                if (TreeView::isBranchLinesEnabled(treeView) && qobject_cast<BranchDelegate*>(treeView->itemDelegate()))
                {
                    auto copy = *option;
                    copy.rect.adjust(treeView->indentation(), 0, treeView->indentation(), 0);
                    return QProxyStyle::subElementRect(element, &copy, widget);
                }
            }
            break;
        }

        return QProxyStyle::subElementRect(element, option, widget);
    }

    int Style::pixelMetric(QStyle::PixelMetric metric, const QStyleOption* option,
        const QWidget* widget) const
    {
        if (!hasStyle(widget))
        {
            return QProxyStyle::pixelMetric(metric, option, widget);
        }

        if (StyleManager::stylesheetsDisabled())
        {
            // Indicator footprints from CheckBox.qss / RadioButton.qss (16x16; toggle switch 32x16)
            switch (metric)
            {
                case PM_IndicatorWidth:
                    return (widget && hasClass(widget, QStringLiteral("ToggleSwitch"))) ? 32 : 16;
                case PM_IndicatorHeight:
                case PM_ExclusiveIndicatorWidth:
                case PM_ExclusiveIndicatorHeight:
                    return 16;
                case PM_MenuHMargin:
                    // Menu.qss QMenu padding: 4px 2px (horizontal component)
                    return 2;
                case PM_MenuVMargin:
                    // Menu.qss QMenu padding: 4px 2px (vertical component)
                    return 4;
                case PM_ScrollBarExtent:
                    // ScrollBar.qss QScrollBar:vertical width / :horizontal height
                    return 8;
                case PM_ScrollBarSliderMin:
                    // ScrollBar.qss handle min-height/min-width
                    return 32;
                case PM_SplitterWidth:
                    // qss used 1px visual; 4px keeps the handle grabbable
                    return 4;
                case PM_ToolBarIconSize:
                    // ToolBar.qss qproperty-iconSize tiers: default 16, IconLarge 20,
                    // MainToolBar 20, MainToolBar+IconLarge 32
                    if (widget && hasClass(widget, QStringLiteral("MainToolBar")))
                    {
                        return hasClass(widget, QStringLiteral("IconLarge")) ? 32 : 20;
                    }
                    return (widget && hasClass(widget, QStringLiteral("IconLarge"))) ? 20 : 16;
                default:
                    break;
            }
        }

        switch (metric)
        {
            case QStyle::PM_ButtonMargin:
            {
                int margin = ToolButton::buttonMargin(this, option, widget, m_data->toolButtonConfig);
                if (margin != -1)
                {
                    return margin;
                }

                margin = PushButton::buttonMargin(this, option, widget, m_data->pushButtonConfig);
                if (margin != -1)
                {
                    return margin;
                }

                break;
            }

            case QStyle::PM_LayoutLeftMargin:
            case QStyle::PM_LayoutTopMargin:
            case QStyle::PM_LayoutRightMargin:
            case QStyle::PM_LayoutBottomMargin:
                return 5;

            case QStyle::PM_LayoutHorizontalSpacing:
            case QStyle::PM_LayoutVerticalSpacing:
                return 3;

            case QStyle::PM_HeaderDefaultSectionSizeVertical:
                return 24;

            case QStyle::PM_DefaultFrameWidth:
            {
                if (auto button = qobject_cast<const QToolButton*>(widget))
                {
                    if (button->popupMode() == QToolButton::MenuButtonPopup)
                    {
                        return 0;
                    }
                }

                break;
            }

            case QStyle::PM_ButtonIconSize:
            {
                int size = ToolButton::buttonIconSize(this, option, widget, m_data->toolButtonConfig);
                if (size != -1)
                {
                    return size;
                }
                return 24;
                break;
            }

            case QStyle::PM_ToolBarItemSpacing:
            {
                int spacing = ToolBar::itemSpacing(this, option, widget, m_data->toolBarConfig);
                if (spacing != -1)
                {
                    return spacing;
                }
                break;
            }

            case QStyle::PM_DockWidgetSeparatorExtent:
                return 3;
                break;

            case QStyle::PM_SliderThickness:
            case QStyle::PM_SliderControlThickness: // used by qCommonStyle::subControlRect()
            {
                int thickness = Slider::sliderThickness(this, option, widget, m_data->sliderConfig);
                if (thickness != -1)
                {
                    return thickness;
                }
                break;
            }

            case QStyle::PM_SliderLength:
            {
                int length = Slider::sliderLength(this, option, widget, m_data->sliderConfig);
                if (length != -1)
                {
                    return length;
                }
                break;
            }

            case QStyle::PM_TitleBarHeight:
            {
                const int height = TitleBar::titleBarHeight(this, option, widget, m_data->titleBarConfig, m_data->tabWidgetConfig);
                if (height != -1)
                {
                    return height;
                }
                break;
            }

            case QStyle::PM_TabCloseIndicatorWidth:
            case QStyle::PM_TabCloseIndicatorHeight:
            {
                return TabBar::closeButtonSize(this, option, widget, m_data->tabWidgetConfig);
                break;
            }

            case QStyle::PM_MenuButtonIndicator:
            {
                int size = ToolButton::menuButtonIndicatorWidth(this, option, widget, m_data->toolButtonConfig);
                if (size != -1)
                {
                    return size;
                }
                size = PushButton::menuButtonIndicatorWidth(this, option, widget, m_data->pushButtonConfig);
                if (size != -1)
                {
                    return size;
                }
                break;
            }

            case QStyle::PM_SubMenuOverlap:
            {
                const int overlap = Menu::subMenuOverlap(this, option, widget, m_data->menuConfig);
                if (overlap != std::numeric_limits<int>::lowest())
                {
                    return overlap;
                }
                break;
            }

            case QStyle::PM_ToolBarExtensionExtent:
            {
                int retval{ 12 };
                return retval;
            }

            default:
                break;
        }

        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    // Defined with the application-polish section further down.
    static QPalette buildO3DEBasePalette();
    static QPalette buildO3DEMenuPalette(const QPalette& basePalette);

    //////////////////////////////////////////////////////////////////////////
    // Flattened typography (GUI flattening)
    //
    // Text.qss set per-class font sizes and colors on QLabel via the
    // Text::add*Style/add*Color class tags. The app-wide Open Sans 12px font
    // already covers Label/Menu/Paragraph/Tooltip; only the larger tiers and
    // the color classes need explicit owners here.
    //////////////////////////////////////////////////////////////////////////
    static void applyFlattenedTypographyStyle(QWidget* widget)
    {
        if (!widget->inherits("QLabel"))
        {
            return;
        }

        int pixelSize = 0;
        if (Style::hasClass(widget, QStringLiteral("Headline")))
        {
            pixelSize = 24;
        }
        else if (Style::hasClass(widget, QStringLiteral("Title")))
        {
            pixelSize = 18;
        }
        else if (Style::hasClass(widget, QStringLiteral("Subtitle")))
        {
            pixelSize = 16;
        }

        if (pixelSize > 0)
        {
            QFont classFont = widget->font();
            classFont.setPixelSize(pixelSize);
            widget->setFont(classFont);
        }

        QColor textColor;
        if (Style::hasClass(widget, QStringLiteral("secondaryText")))
        {
            textColor = QColor(0x88, 0x88, 0x88);
        }
        else if (Style::hasClass(widget, QStringLiteral("highlightedText")))
        {
            textColor = QColor(0x44, 0xB2, 0xF8);
        }
        else if (Style::hasClass(widget, QStringLiteral("blackText")))
        {
            textColor = QColor(0x00, 0x00, 0x00);
        }

        if (textColor.isValid())
        {
            QPalette labelPalette = widget->palette();
            labelPalette.setColor(QPalette::WindowText, textColor);
            labelPalette.setColor(QPalette::Text, textColor);
            widget->setPalette(labelPalette);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Flattened text-entry palette (GUI flattening)
    //
    // Mirrors the BaseStyleSheet.qss rule that gave text-entry widgets a
    // light background with black text (QLineEdit/QSpinBox/QDoubleSpinBox/
    // QComboBox: #CCCCCC + black; QTextEdit/QPlainTextEdit: #E9E9E9 +
    // #545454). Applied per widget class because the app-wide palette keeps
    // QPalette::Base dark for the item views.
    //////////////////////////////////////////////////////////////////////////
    static void applyFlattenedTextEntryPalette(QWidget* widget)
    {
        const bool isLineEntry = widget->inherits("QLineEdit") || widget->inherits("QAbstractSpinBox") || widget->inherits("QComboBox");
        const bool isTextEdit = widget->inherits("QTextEdit") || widget->inherits("QPlainTextEdit");

        if (!isLineEntry && !isTextEdit)
        {
            return;
        }

        QPalette entryPalette = widget->palette();
        const QColor base = isTextEdit ? QColor(0xE9, 0xE9, 0xE9) : QColor(0xCC, 0xCC, 0xCC);
        const QColor text = isTextEdit ? QColor(0x54, 0x54, 0x54) : QColor(Qt::black);

        entryPalette.setColor(QPalette::Active, QPalette::Base, base);
        entryPalette.setColor(QPalette::Inactive, QPalette::Base, base);
        entryPalette.setColor(QPalette::Active, QPalette::Text, text);
        entryPalette.setColor(QPalette::Inactive, QPalette::Text, text);
        entryPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(0x66, 0x66, 0x66));   // QLineEdit:disabled background
        entryPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x99, 0x99, 0x99));   // QLineEdit:disabled color

        if (widget->inherits("QComboBox") || widget->inherits("QAbstractSpinBox"))
        {
            // Fusion paints the closed combo face / spin buttons with Button/ButtonText;
            // keep them light like the QSS look so labels are not black-on-dark.
            entryPalette.setColor(QPalette::Active, QPalette::Button, base);
            entryPalette.setColor(QPalette::Inactive, QPalette::Button, base);
            entryPalette.setColor(QPalette::Active, QPalette::ButtonText, text);
            entryPalette.setColor(QPalette::Inactive, QPalette::ButtonText, text);
            entryPalette.setColor(QPalette::Disabled, QPalette::Button, QColor(0x66, 0x66, 0x66));
            entryPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x99, 0x99, 0x99));
        }

        widget->setPalette(entryPalette);
    }

    void Style::polish(QWidget* widget)
    {
        static QWidget* alreadyStyling = nullptr;
        if (alreadyStyling == widget)
        {
            return;
        }

        QScopedValueRollback<QWidget*> recursionGuard(alreadyStyling, widget);

        if (StyleManager::stylesheetsDisabled())
        {
            applyFlattenedTextEntryPalette(widget);

            // Combo box popups share the menu chrome (Menu.qss #222222); the
            // container's palette propagates to the popup list view inside it.
            if (widget->inherits("QComboBoxPrivateContainer"))
            {
                widget->setPalette(buildO3DEMenuPalette(widget->palette()));
            }

            // Progress bar track and fill (ProgressBar.qss: track #9A9A9A, chunk #1E70EB,
            // height 5px)
            if (widget->inherits("QProgressBar"))
            {
                QPalette progressPalette = widget->palette();
                progressPalette.setColor(QPalette::Base, QColor(0x9A, 0x9A, 0x9A));
                progressPalette.setColor(QPalette::Highlight, QColor(0x1E, 0x70, 0xEB));
                progressPalette.setColor(QPalette::HighlightedText, QColor(Qt::white));
                widget->setPalette(progressPalette);
                widget->setMaximumHeight(5);
            }

            // Scroll bars need WA_Hover for the hover lane fill; in qss mode the
            // CSS engine enabled it automatically for any widget with a :hover rule.
            if (widget->inherits("QScrollBar"))
            {
                widget->setAttribute(Qt::WA_Hover, true);
            }

            // Splitter handles need WA_Hover for the hover fill, same as scroll bars
            if (widget->inherits("QSplitterHandle"))
            {
                widget->setAttribute(Qt::WA_Hover, true);
            }

            // Toolbars: ToolBar.qss iconSize tiers + homogeneous 5px padding.
            // Set explicitly - QToolBar caches its icon size.
            if (auto toolBar = qobject_cast<QToolBar*>(widget))
            {
                int iconExtent = 16;
                if (hasClass(widget, QStringLiteral("MainToolBar")))
                {
                    iconExtent = hasClass(widget, QStringLiteral("IconLarge")) ? 32 : 20;
                }
                else if (hasClass(widget, QStringLiteral("IconLarge")))
                {
                    iconExtent = 20;
                }
                toolBar->setIconSize(QSize(iconExtent, iconExtent));
                if (toolBar->layout())
                {
                    toolBar->layout()->setContentsMargins(5, 5, 5, 5);
                }
            }

            // TabWidget.qss: the TabWidget itself is the #111111 backdrop; the stacked
            // page area sits on #444444
            if (widget->inherits("AzQtComponents::TabWidget"))
            {
                QPalette tabWidgetPalette = widget->palette();
                tabWidgetPalette.setColor(QPalette::Window, QColor(0x11, 0x11, 0x11));
                widget->setPalette(tabWidgetPalette);
                widget->setAutoFillBackground(true);
            }
            if (widget->inherits("QStackedWidget") && widget->parentWidget()
                && widget->parentWidget()->inherits("AzQtComponents::TabWidget"))
            {
                QPalette pageAreaPalette = widget->palette();
                pageAreaPalette.setColor(QPalette::Window, QColor(0x44, 0x44, 0x44));
                widget->setPalette(pageAreaPalette);
                widget->setAutoFillBackground(true);
            }

            // Tab bars: qss-era #111111 row backdrop + no native base line. DockTabBar
            // (docked panel strips) keeps its own treatment.
            if (widget->inherits("QTabBar") && !widget->inherits("AzQtComponents::DockTabBar"))
            {
                if (auto tabBar = qobject_cast<QTabBar*>(widget))
                {
                    tabBar->setDrawBase(false);
                }
                QPalette tabPalette = widget->palette();
                tabPalette.setColor(QPalette::Window, QColor(0x11, 0x11, 0x11));
                widget->setPalette(tabPalette);
                widget->setAutoFillBackground(true);
            }

            // Headers: TableView.qss flat #2d2d2d sections (Fusion draws from Button)
            if (widget->inherits("QHeaderView"))
            {
                QPalette headerPalette = widget->palette();
                headerPalette.setColor(QPalette::Button, QColor(0x2D, 0x2D, 0x2D));
                headerPalette.setColor(QPalette::ButtonText, QColor(0xCC, 0xCC, 0xCC));
                widget->setPalette(headerPalette);
            }

            // Text.qss class-driven typography
            applyFlattenedTypographyStyle(widget);

            // Scroll area corner tile: mirrors the qss [ShowBackground=true] rule.
            // The watcher toggles the property on hover and re-polishes.
            if (widget->property("ShowBackground").isValid())
            {
                const QWidget* cornerScrollArea = widget->parentWidget();
                const bool darkCorner = cornerScrollArea && Style::hasClass(cornerScrollArea, QStringLiteral("DarkScrollBar"));
                const bool showCorner = widget->property("ShowBackground").toBool();

                QPalette cornerPalette = widget->palette();
                cornerPalette.setColor(QPalette::Window, showCorner
                    ? (darkCorner ? QColor(0xDC, 0xDC, 0xDC) : QColor(0x55, 0x55, 0x55))
                    : QColor(Qt::transparent));
                widget->setAutoFillBackground(showCorner);
                widget->setPalette(cornerPalette);
            }
        }

        if (hasStyle(widget))
        {
            bool polishedAlready = false;

            polishedAlready = polishedAlready || PushButton::polish(this, widget, m_data->pushButtonConfig);
            polishedAlready = polishedAlready || CheckBox::polish(this, widget, m_data->checkBoxConfig);
            polishedAlready = polishedAlready || RadioButton::polish(this, widget, m_data->radioButtonConfig);
            polishedAlready = polishedAlready || Slider::polish(this, widget, m_data->sliderConfig);
            polishedAlready = polishedAlready || Card::polish(this, widget, m_data->cardConfig);
            polishedAlready = polishedAlready || ColorPicker::polish(this, widget, m_data->colorPickerConfig);
            polishedAlready = polishedAlready || Eyedropper::polish(this, widget, m_data->eyedropperConfig);
            polishedAlready = polishedAlready || BreadCrumbs::polish(this, widget, m_data->breadCrumbsConfig);
            polishedAlready = polishedAlready || PaletteView::polish(this, widget, m_data->paletteViewConfig);
            polishedAlready = polishedAlready || VectorElement::polish(this, widget, m_data->spinBoxConfig);
            polishedAlready = polishedAlready || SpinBox::polish(this, widget, m_data->spinBoxConfig);
            polishedAlready = polishedAlready || LineEdit::polish(this, widget, m_data->lineEditConfig);
            polishedAlready = polishedAlready || BrowseEdit::polish(this, widget, m_data->browseEditConfig, m_data->lineEditConfig);
            polishedAlready = polishedAlready || ComboBox::polish(this, widget, m_data->comboBoxConfig);
            polishedAlready = polishedAlready || AssetFolderThumbnailView::polish(this, widget, m_data->scrollBarConfig, m_data->assetFolderThumbnailViewConfig);
            polishedAlready = polishedAlready || FilteredSearchWidget::polish(this, widget, m_data->filteredSearchWidgetConfig);
            polishedAlready = polishedAlready || TableView::polish(this, widget, m_data->scrollBarConfig, m_data->tableViewConfig);
            polishedAlready = polishedAlready || TitleBar::polish(this, widget, m_data->titleBarConfig);
            polishedAlready = polishedAlready || TabBar::polish(this, widget, m_data->tabWidgetConfig);
            polishedAlready = polishedAlready || TabWidget::polish(this, widget, m_data->tabWidgetConfig);
            polishedAlready = polishedAlready || Menu::polish(this, widget, m_data->menuConfig);
            polishedAlready = polishedAlready || ToolButton::polish(this, widget, m_data->toolButtonConfig);
            polishedAlready = polishedAlready || StyledBusyLabel::polish(this, widget);
            polishedAlready = polishedAlready || ToolBar::polish(this, widget, m_data->toolBarConfig);
            polishedAlready = polishedAlready || TreeView::polish(this, widget, m_data->scrollBarConfig, m_data->treeViewConfig);
            polishedAlready = polishedAlready || DialogButtonBox::polish(this, widget);

            // A number of classes derive from QAbstractScrollArea. If one of these classes requires
            // polishing ensure that their polish function calls ScrollBar::polish.
            // ScrollBar::polish must be done last otherwise it will trap the event before it gets
            // to derived classes.
            polishedAlready = polishedAlready || ScrollBar::polish(this, widget, m_data->scrollBarConfig);
        }

        QProxyStyle::polish(widget);
    }

    void Style::unpolish(QWidget* widget)
    {
        static QWidget* alreadyUnStyling = nullptr;
        if (alreadyUnStyling == widget)
        {
            return;
        }

        QScopedValueRollback<QWidget*> recursionGuard(alreadyUnStyling, widget);

        if (hasStyle(widget))
        {
            bool unpolishedAlready = false;

            unpolishedAlready = unpolishedAlready || Card::unpolish(this, widget, m_data->cardConfig);
            unpolishedAlready = unpolishedAlready || VectorElement::unpolish(this, widget, m_data->spinBoxConfig);
            unpolishedAlready = unpolishedAlready || SpinBox::unpolish(this, widget, m_data->spinBoxConfig);
            unpolishedAlready = unpolishedAlready || LineEdit::unpolish(this, widget, m_data->lineEditConfig);
            unpolishedAlready = unpolishedAlready || BrowseEdit::unpolish(this, widget, m_data->browseEditConfig, m_data->lineEditConfig);
            unpolishedAlready = unpolishedAlready || ComboBox::unpolish(this, widget, m_data->comboBoxConfig);
            unpolishedAlready = unpolishedAlready || FilteredSearchWidget::unpolish(this, widget, m_data->filteredSearchWidgetConfig);
            unpolishedAlready = unpolishedAlready || TableView::unpolish(this, widget, m_data->scrollBarConfig, m_data->tableViewConfig);
            unpolishedAlready = unpolishedAlready || TitleBar::unpolish(this, widget, m_data->titleBarConfig);
            unpolishedAlready = unpolishedAlready || TabBar::unpolish(this, widget, m_data->tabWidgetConfig);
            unpolishedAlready = unpolishedAlready || TabWidget::unpolish(this, widget, m_data->tabWidgetConfig);
            unpolishedAlready = unpolishedAlready || Menu::unpolish(this, widget, m_data->menuConfig);
            unpolishedAlready = unpolishedAlready || StyledBusyLabel::unpolish(this, widget);

            // A number of classes derive from QAbstractScrollArea. If one of these classes requires
            // unpolishing ensure that their unpolish function calls ScrollBar::polish.
            // ScrollBar::unpolish must be done last otherwise it will trap the event before it gets
            // to derived classes.
            unpolishedAlready = unpolishedAlready || ScrollBar::unpolish(this, widget, m_data->scrollBarConfig);
        }

        QProxyStyle::unpolish(widget);
    }
    
    void Style::polish(QPalette& palette)
    {
        QProxyStyle::polish(palette);

        if (StyleManager::stylesheetsDisabled())
        {
            // Every palette Qt derives (including system color-scheme changes)
            // passes through here - replacing it wholesale is what guarantees
            // the engine's forced theme and disables reactive dark/light mode.
            palette = buildO3DEBasePalette();
        }
    }
    
    void Style::unpolish(QApplication* application)
    {
        QProxyStyle::unpolish(application);
    }

    QPalette Style::standardPalette() const
    {
        return m_data->palette;
    }

    QIcon Style::standardIcon(QStyle::StandardPixmap standardIcon, const QStyleOption* option, const QWidget* widget) const
    {
        if (!hasStyle(widget))
        {
            return QProxyStyle::standardIcon(standardIcon, option, widget);
        }

        switch (standardIcon)
        {
            case QStyle::SP_LineEditClearButton:
            {
                const QLineEdit* le = qobject_cast<const QLineEdit*>(widget);
                if (le)
                {
                    return LineEdit::clearButtonIcon(option, widget, m_data->lineEditConfig);
                }
            }
            break;

            case QStyle::SP_MessageBoxInformation:
                return QIcon(QString::fromUtf8(":/stylesheet/img/UI20/Info.svg"));
                break;

            // Title bar button glyphs (TitleBar.qss titlebar-*-icon rules). Only
            // reachable when no QStyleSheetStyle intercepts standardIcon, i.e. in
            // flattened mode - same pattern as SP_MessageBoxInformation above.
            case QStyle::SP_TitleBarCloseButton:
                return QIcon(QStringLiteral(":/Application/titlebar-close.svg"));

            case QStyle::SP_TitleBarMinButton:
                return QIcon(QStringLiteral(":/Application/titlebar-minimize.svg"));

            case QStyle::SP_TitleBarNormalButton:
                return QIcon(QStringLiteral(":/Application/titlebar-restore.svg"));

            case QStyle::SP_TitleBarMaxButton:
                return QIcon(Style::hasClass(widget, QStringLiteral("restore"))
                    ? QStringLiteral(":/Application/titlebar-restore.svg")
                    : QStringLiteral(":/Application/titlebar-maximize.svg"));

            default:
                break;
        }
        return QProxyStyle::standardIcon(standardIcon, option, widget);
    }

    int Style::styleHint(QStyle::StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const
    {
        if (hint == QStyle::SH_SpinBox_StepModifier)
        {
            return Qt::ShiftModifier;
        }

        // Fusion defaults SH_ComboBox_Popup to menu-mode, which draws a checkmark on
        // the current item stacked over the field. The qss-era rendering was list-mode
        // (no checkmarks, pre-highlighted current row) - force it.
        if (hint == QStyle::SH_ComboBox_Popup && StyleManager::stylesheetsDisabled())
        {
            return 0;
        }

        if (!hasStyle(widget))
        {
            return QProxyStyle::styleHint(hint, option, widget, returnData);
        }

        if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
        {
            return Slider::styleHintAbsoluteSetButtons();
        }
        else if (hint == QStyle::SH_Menu_SubMenuPopupDelay)
        {
            // Default to sub-menu pop-up delay of 0 (for instant drawing of submenus, Qt defaults to 225 ms)
            const int defaultSubMenuPopupDelay = 0;
            return defaultSubMenuPopupDelay;
        }
        else if (hint == QStyle::SH_ComboBox_PopupFrameStyle)
        {
            // We want popup like combobox to have no frame
            return QFrame::NoFrame;
        }
        else if (hint == QStyle::SH_ComboBox_Popup)
        {
            // We want popup like combobox
            return 0;
        }
        else if (hint == QStyle::SH_ComboBox_UseNativePopup)
        {
            // We want non native popup like combobox
            return 0;
        }

        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    QPainterPath Style::borderLineEditRect(const QRect& contentsRect, int borderWidth, int borderRadius) const
    {
        const auto borderAdjustment = borderRadius - borderWidth;
        QPainterPath pathRect;

        if (borderRadius != BorderStyle::CORNER_RECTANGLE)
        {
            const auto radius = borderRadius + borderWidth;
            pathRect.addRoundedRect(contentsRect.adjusted(borderAdjustment,
                                        borderAdjustment,
                                        -borderAdjustment,
                                        -borderAdjustment),
                radius, radius);
        }
        else
        {
            pathRect.addRect(contentsRect.adjusted(borderAdjustment,
                borderAdjustment,
                -borderAdjustment,
                -borderAdjustment));
        }

        return pathRect;
    }

    QPainterPath Style::lineEditRect(const QRect& contentsRect, int borderWidth, int borderRadius) const
    {
        QPainterPath pathRect;

        if (borderRadius != BorderStyle::CORNER_RECTANGLE)
        {
            pathRect.addRoundedRect(contentsRect.adjusted(borderWidth,
                                        borderWidth,
                                        -borderWidth,
                                        -borderWidth),
                borderRadius, borderRadius);
        }
        else
        {
            pathRect.addRect(contentsRect.adjusted(borderWidth,
                borderWidth,
                -borderWidth,
                -borderWidth));
        }

        return pathRect;
    }

    void Style::repolishOnSettingsChange(QWidget* widget)
    {
        // don't listen twice for the settingsReloaded signal on the same widget
        if (m_data->widgetsToRepolishOnReload.contains(widget))
        {
            return;
        }

        m_data->widgetsToRepolishOnReload.insert(widget);

        // Qt::UniqueConnection doesn't work with lambdas, so we have to track this ourselves

        QObject::connect(widget, &QObject::destroyed, this, &Style::repolishWidgetDestroyed);
        QObject::connect(this, &Style::settingsReloaded, widget, [widget]() {
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
        });
    }

    bool Style::eventFilter(QObject* watched, QEvent* ev)
    {
        switch (ev->type())
        {
            case QEvent::ToolTipChange:
            {
                if (QWidget* w = qobject_cast<QWidget*>(watched))
                {
                    forceToolTipLineWrap(w);
                }
            }
            break;
        }

        const bool flagToContinueProcessingEvent = false;
        return flagToContinueProcessingEvent;
    }

    bool Style::hasClass(const QWidget* button, const QString& className)
    {
        QVariant buttonClassVariant = button->property("class");
        if (buttonClassVariant.isNull())
        {
            return false;
        }

        QString classText = buttonClassVariant.toString();
        QStringList classList = classText.split(QRegularExpression("\\s+"));
        return classList.contains(className, Qt::CaseInsensitive);
    }

    void Style::addClass(QWidget* button, const QString& className)
    {
        QVariant buttonClassVariant = button->property("class");
        if (buttonClassVariant.isNull())
        {
            button->setProperty("class", className);
        }
        else
        {
            QString classText = buttonClassVariant.toString();
            classText.append(QStringLiteral(" %1").arg(className));
            button->setProperty("class", classText);
        }

        button->style()->unpolish(button);
        button->style()->polish(button);
    }

    void Style::removeClass(QWidget* button, const QString& className)
    {
        QVariant buttonClassVariant = button->property("class");
        if (!buttonClassVariant.isNull())
        {
            const QString classText = buttonClassVariant.toString();
            QStringList classList = classText.split(QRegularExpression("\\s+"));
            bool changed = false;
            for (int i = static_cast<int>(classList.count()) -1; i >= 0; --i)
            {
                if (classList[i].compare(className, Qt::CaseInsensitive) == 0)
                {
                    classList.removeAt(i);
                    changed = true;
                }
            }
            if (changed)
            {
                button->setProperty("class", classList.join(QLatin1Char(' ')));
                button->style()->unpolish(button);
                button->style()->polish(button);
            }
        }
    }

    QPixmap Style::cachedPixmap(const QString& name)
    {
        QPixmap pixmap;

        if (!QPixmapCache::find(name, &pixmap))
        {
            pixmap = QPixmap(name);
            QPixmapCache::insert(name, pixmap);
        }

        return pixmap;
    }

    void Style::drawFrame(QPainter* painter, const QPainterPath& frameRect, const QPen& border, const QBrush& background)
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(border);
        painter->setBrush(background);
        painter->drawPath(frameRect);
        painter->restore();
    }

    void Style::flagToIgnore(QWidget* widget)
    {
        widget->setProperty(g_removeAllStylingProperty, true);
    }

    void Style::removeFlagToIgnore(QWidget* widget)
    {
        widget->setProperty(g_removeAllStylingProperty, QVariant());
    }

    bool Style::hasStyle(const QWidget* widget)
    {
        return (widget == nullptr) || widget->property(g_removeAllStylingProperty).isNull();
    }

    void Style::prepPainter(QPainter* painter)
    {
        // HACK:
        // QPainter is not guaranteed to have its QPaintEngine initialized in setRenderHint,
        // so go ahead and call save/restore here which ensures that.
        // See: QTBUG-51247
        painter->save();
        painter->restore();
    }

    void Style::fixProxyStyle(QProxyStyle* proxyStyle, QStyle* baseStyle)
    {
        QStyle* applicationStyle = qApp->style();
        QObject* oldParent = applicationStyle->parent();
        proxyStyle->setBaseStyle(baseStyle);
        if (baseStyle == applicationStyle)
        {
            // WORKAROUND: A QProxyStyle over qApp->style() is bad practice as both classes want the ownership over the base style, leading to possible crashes
            // Ideally all this custom styling should be moved to Style.cpp, as a new "style class"
            applicationStyle->setParent(oldParent); // Restore damage done by QProxyStyle
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // O3DE base application palette (GUI flattening)
    //
    // The single explicit owner of every application-wide color in the
    // flattened (no style sheet) mode. Built FROM SCRATCH - never derived
    // from the incoming system palette - because O3DE does not do reactive
    // OS dark/light theming: the engine FORCES its theme, and a "dark theme"
    // is simply a theme that is dark. Every value below mirrors a rule that
    // BaseStyleSheet.qss used to apply - the source rule is noted per line
    // and tracked in the flatten ledger. Fusion consumes this palette for all
    // base widget painting.
    //
    // Note on QPalette::Base: O3DE's look is dark item views with LIGHT text
    // entry fields, but Qt shares the Base role between both. The app-wide
    // palette keeps Base dark for the views; text-entry widget classes get
    // their light palette in Style::polish(QWidget*) below, mirroring the
    // QLineEdit/QSpinBox/QComboBox rule from BaseStyleSheet.qss.
    //////////////////////////////////////////////////////////////////////////
    static QPalette buildO3DEBasePalette()
    {
        QPalette palette;
        // Core surfaces
        const QColor window(0x44, 0x44, 0x44);          // QMainWindow/QDialog/QDockWidget background-color: #444444
        const QColor viewBase(0x44, 0x44, 0x44);        // parentless QTableView/QListView/QTreeView background-color: #444444
        const QColor alternateBase(0x4D, 0x4D, 0x4D);   // TableView.qss alternate-background-color: rgb(77,77,77)
        const QColor textWhite(0xFF, 0xFF, 0xFF);       // global '*' rule color: white
        const QColor disabledText(0x99, 0x99, 0x99);    // QLineEdit:disabled color: #999999
        const QColor disabledBase(0x66, 0x66, 0x66);    // QLineEdit:disabled background-color: #666666

        // Buttons and the 3D bevel ladder (Fusion derives frame/bevel shading from these)
        const QColor button(0x55, 0x55, 0x55);          // mid button surface (Original shared palette)
        const QColor light(0x66, 0x66, 0x66);
        const QColor midlight(0x55, 0x55, 0x55);
        const QColor mid(0x33, 0x33, 0x33);
        const QColor dark(0x22, 0x22, 0x22);
        const QColor shadow(0x11, 0x11, 0x11);          // QMainWindow:separator background-color: #111111

        // Selection (TableView.qss selection-background-color / selection-color)
        const QColor highlight(0x65, 0x65, 0x65);       // rgb(101,101,101)
        const QColor highlightedText(0xFF, 0xFF, 0xFF);

        // Tooltips (ToolTip.qss)
        const QColor toolTipBase(0x00, 0x00, 0x00);
        const QColor toolTipText(0xFF, 0xFF, 0xFF);

        // Hyperlinks (Text config hyperlinkColor re-applies over this at app polish)
        const QColor link(0x94, 0xD2, 0xFF);

        // Active and Inactive groups render identically in O3DE
        for (const auto group : { QPalette::Active, QPalette::Inactive })
        {
            palette.setColor(group, QPalette::Window, window);
            palette.setColor(group, QPalette::WindowText, textWhite);
            palette.setColor(group, QPalette::Base, viewBase);
            palette.setColor(group, QPalette::AlternateBase, alternateBase);
            palette.setColor(group, QPalette::Text, textWhite);
            palette.setColor(group, QPalette::PlaceholderText, disabledText);
            palette.setColor(group, QPalette::Button, button);
            palette.setColor(group, QPalette::ButtonText, textWhite);
            palette.setColor(group, QPalette::BrightText, textWhite);
            palette.setColor(group, QPalette::Light, light);
            palette.setColor(group, QPalette::Midlight, midlight);
            palette.setColor(group, QPalette::Mid, mid);
            palette.setColor(group, QPalette::Dark, dark);
            palette.setColor(group, QPalette::Shadow, shadow);
            palette.setColor(group, QPalette::Highlight, highlight);
            palette.setColor(group, QPalette::HighlightedText, highlightedText);
            palette.setColor(group, QPalette::ToolTipBase, toolTipBase);
            palette.setColor(group, QPalette::ToolTipText, toolTipText);
            palette.setColor(group, QPalette::Link, link);
            palette.setColor(group, QPalette::LinkVisited, link);
        }

        // Disabled group
        palette.setColor(QPalette::Disabled, QPalette::Window, window);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::Base, disabledBase);
        palette.setColor(QPalette::Disabled, QPalette::AlternateBase, alternateBase);
        palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::Button, button);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::BrightText, textWhite);
        palette.setColor(QPalette::Disabled, QPalette::Light, light);
        palette.setColor(QPalette::Disabled, QPalette::Midlight, midlight);
        palette.setColor(QPalette::Disabled, QPalette::Mid, mid);
        palette.setColor(QPalette::Disabled, QPalette::Dark, dark);
        palette.setColor(QPalette::Disabled, QPalette::Shadow, shadow);
        palette.setColor(QPalette::Disabled, QPalette::Highlight, highlight);
        palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::ToolTipBase, toolTipBase);
        palette.setColor(QPalette::Disabled, QPalette::ToolTipText, toolTipText);
        palette.setColor(QPalette::Disabled, QPalette::Link, link);
        palette.setColor(QPalette::Disabled, QPalette::LinkVisited, link);

        return palette;
    }

    //////////////////////////////////////////////////////////////////////////
    // Menu popups are core chrome that appears everywhere, so their dark
    // surface (Menu.qss: QMenu background-color #222222, selected item
    // #444444) is owned here as a QMenu class palette rather than waiting
    // for the Menu family batch. Applied AFTER the global setPalette call,
    // which clears class palettes.
    //////////////////////////////////////////////////////////////////////////
    static QPalette buildO3DEMenuPalette(const QPalette& basePalette)
    {
        QPalette menuPalette = basePalette;
        const QColor menuSurface(0x22, 0x22, 0x22);     // Menu.qss QMenu background-color: #222222
        const QColor menuHighlight(0x44, 0x44, 0x44);   // Menu.qss selected item background: #444444
        const QColor menuText(0xFF, 0xFF, 0xFF);
        const QColor menuDisabledText(0x55, 0x55, 0x55);    // Menu.qss disabled item color: #555555

        for (const auto group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
        {
            menuPalette.setColor(group, QPalette::Window, menuSurface);
            menuPalette.setColor(group, QPalette::Base, menuSurface);
            menuPalette.setColor(group, QPalette::Highlight, menuHighlight);
        }

        // Force text roles: popups can inherit a light text-entry palette (combo box
        // popup containers inherit from the combo box), which would leave near-black
        // text on the dark menu surface.
        for (const auto group : { QPalette::Active, QPalette::Inactive })
        {
            menuPalette.setColor(group, QPalette::Text, menuText);
            menuPalette.setColor(group, QPalette::WindowText, menuText);
            menuPalette.setColor(group, QPalette::ButtonText, menuText);
            menuPalette.setColor(group, QPalette::HighlightedText, menuText);
        }
        menuPalette.setColor(QPalette::Disabled, QPalette::Text, menuDisabledText);
        menuPalette.setColor(QPalette::Disabled, QPalette::WindowText, menuDisabledText);
        menuPalette.setColor(QPalette::Disabled, QPalette::ButtonText, menuDisabledText);
        menuPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, menuDisabledText);

        return menuPalette;
    }

    void Style::polish(QApplication* application)
    {
        Q_UNUSED(application);

        if (StyleManager::stylesheetsDisabled())
        {
            // Forced O3DE palette - never derived from the system palette (no
            // reactive OS dark/light theming; the engine owns its look).
            m_data->palette = buildO3DEBasePalette();
        }
        else
        {
            m_data->palette = application->palette();
        }

        m_data->palette.setColor(QPalette::Link, m_data->textConfig.hyperlinkColor);

        application->setPalette(m_data->palette);

        if (StyleManager::stylesheetsDisabled())
        {
            // Class palettes must be applied after the global setPalette (which clears them)
            application->setPalette(buildO3DEMenuPalette(m_data->palette), "QMenu");

            // Menu bar strip: dark surface (user-verified vs baseline), selected item
            // #555555 (MenuBar.qss)
            QPalette menuBarPalette = m_data->palette;
            const QColor menuBarSurface(0x22, 0x22, 0x22);
            for (const auto group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
            {
                menuBarPalette.setColor(group, QPalette::Window, menuBarSurface);
                menuBarPalette.setColor(group, QPalette::Base, menuBarSurface);
                menuBarPalette.setColor(group, QPalette::Button, menuBarSurface);
                menuBarPalette.setColor(group, QPalette::Highlight, QColor(0x55, 0x55, 0x55));
            }
            application->setPalette(menuBarPalette, "QMenuBar");
        }

        // need to listen to and fix tooltips so that they wrap
        application->installEventFilter(this);
        application->setEffectEnabled(Qt::UI_AnimateCombo, false);

        QProxyStyle::polish(application);
    }

    void Style::repolishWidgetDestroyed(QObject* obj)
    {
        m_data->widgetsToRepolishOnReload.remove(obj);
    }

#ifdef _DEBUG
    bool Style::event(QEvent* ev)
    {
        if (ev->type() == QEvent::ParentChange)
        {
            // QApplication owns its style. If a QProxyStyle steals it it might crash, as QProxyStyle also owns its base style.
            // Let's assert to detect this early on

            bool ownershipStolenByProxyStyle = (this == qApp->style()) && qobject_cast<QProxyStyle*>(parent());
            Q_ASSERT(!ownershipStolenByProxyStyle);
        }

        return QProxyStyle::event(ev);
    }
#endif

} // namespace AzQtComponents
