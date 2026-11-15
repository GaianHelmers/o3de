/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzQtComponents/Components/Theme/ThemeEditor.h>

#include <AzCore/Math/Color.h>
#include <AzQtComponents/Components/Theme/ThemeManager.h>
#include <AzQtComponents/Components/Widgets/ColorPicker.h>
#include <AzQtComponents/Utilities/Conversions.h>

#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace AzQtComponents
{
    //! Private to this file: named, not anonymous, as a unity build merges the anonymous namespaces of its files
    namespace ThemeEditorInternal
    {
        // ------------------------------------------------------------------------------
        // Values
        // ------------------------------------------------------------------------------

        constexpr QSize WindowSize(580, 780);       //!< the editor in a window of its own
        constexpr int OuterMargin = 8;
        constexpr int RowSpacing = 4;
        constexpr int GroupIndent = 16;             //!< the rows of a group, in from its title
        constexpr int LabelColumnWidth = 190;
        constexpr int ColorTextWidth = 84;          //!< room for #RRGGBBAA
        constexpr int NumberWidth = 58;
        constexpr int FontWidth = 190;
        constexpr int DialogWidth = 380;
        constexpr QSize SwatchSize(36, 18);
        constexpr int SwatchRadius = 3;
        constexpr int SwatchCheckSize = 4;          //!< the squares behind a translucent colour
        constexpr int NumberLimit = 999;            //!< a length a theme may set, either way
        constexpr int PreviewDelayMs = 40;          //!< a drag in the colour picker shows this often at most
        constexpr const char* FileFilter = "O3DE themes (*.theme.json);;All files (*)";
        constexpr const char* MarginSides[4] = { "Left", "Top", "Right", "Bottom" };

        // ---- Recolor (ThemeEditor::recolored)
        constexpr float ExtremeLightness = 0.02f;   //!< black and white, and what is this close to them, stay
        constexpr float AccentSaturation = 0.35f;   //!< from here up a colour is a colour of its own, below it a grey of the scheme
        constexpr float AccentHueReach = 30.0f / 360.0f;    //!< this far from the accent's hue a colour still turns with it
        constexpr float Epsilon = 0.004f;           //!< less than one step of a colour channel

        // ------------------------------------------------------------------------------
        // The swatch of a colour row
        // ------------------------------------------------------------------------------

        //! A button that shows a colour, over squares where the colour is translucent
        class Swatch
            : public QToolButton
        {
        public:
            explicit Swatch(QWidget* parent)
                : QToolButton(parent)
            {
                setFixedSize(SwatchSize);
                setCursor(Qt::PointingHandCursor);
            }

            void setColor(const QColor& color)
            {
                if (m_color != color)
                {
                    m_color = color;
                    update();
                }
            }

            QColor color() const
            {
                return m_color;
            }

        protected:
            void paintEvent(QPaintEvent*) override
            {
                QPainter painter(this);
                painter.setRenderHint(QPainter::Antialiasing);
                const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
                QPainterPath outline;
                outline.addRoundedRect(box, SwatchRadius, SwatchRadius);
                painter.setClipPath(outline);
                if (m_color.alpha() != 255)
                {
                    painter.fillRect(rect(), Qt::white);
                    for (int y = 0; y < height(); y += SwatchCheckSize)
                    {
                        for (int x = (y / SwatchCheckSize % 2) * SwatchCheckSize; x < width(); x += 2 * SwatchCheckSize)
                        {
                            painter.fillRect(x, y, SwatchCheckSize, SwatchCheckSize, Qt::lightGray);
                        }
                    }
                }
                painter.fillRect(rect(), m_color);
                painter.setClipping(false);
                // The outline reads on any fill: the text colour of the editor, thinned
                QColor line = palette().color(QPalette::WindowText);
                line.setAlpha(underMouse() ? 255 : 110);
                painter.setPen(QPen(line, 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(outline);
            }

        private:
            QColor m_color;
        };

        // ------------------------------------------------------------------------------
        // Helpers
        // ------------------------------------------------------------------------------

        //! "#1e70eb", "1E70EB" and "#1E70EB" are one colour: the text as the registry writes it, or as typed if it is none
        QString colorText(const QString& typed)
        {
            QString text = typed.trimmed();
            if (!text.startsWith(QLatin1Char('#')) && !text.startsWith(QLatin1Char('$')))
            {
                text.prepend(QLatin1Char('#'));
            }
            const QVariant color = ThemeTokens::fromText(ThemeKind::Color, text);
            return color.isValid() ? ThemeTokens::toText(ThemeKind::Color, color) : text;
        }

        QString textOf(const QColor& color)
        {
            return ThemeTokens::toText(ThemeKind::Color, QVariant::fromValue(color));
        }

        QString groupOf(const QByteArray& key)
        {
            return QString::fromUtf8(key.left(key.indexOf('.')));
        }

        //! True if the token takes its value from another: the theme says so (`$key`), or the catalogue does and no theme
        //! gives it a value of its own
        bool follows(const ThemeTokens::Info& token)
        {
            return token.m_text.isEmpty() ? !token.m_source.isEmpty() : token.m_text.startsWith(QLatin1Char('$'));
        }

        //! The distance of two hues round the circle, each 0 to 1
        float hueDistance(float a, float b)
        {
            const float distance = std::fabs(a - b);
            return qMin(distance, 1.0f - distance);
        }

        //! The colour picker over a colour, telling `changed` each colour the user passes through. The colour chosen, or an
        //! invalid one if the dialog was cancelled.
        QColor pickColorLive(QWidget* parent, const QString& title, const QColor& start, const std::function<void(const QColor&)>& changed)
        {
            ColorPicker picker(ColorPicker::Configuration::RGBA, QStringLiteral("ThemeEditor"), parent);
            picker.setWindowTitle(title);
            picker.setCurrentColor(fromQColor(start));
            picker.setSelectedColor(fromQColor(start));
            QObject::connect(&picker, &ColorPicker::currentColorChanged, &picker, [&changed](const AZ::Color& picked)
            {
                changed(toQColor(picked));
            });
            return picker.exec() == QDialog::Accepted ? toQColor(picker.currentColor()) : QColor();
        }

        // ------------------------------------------------------------------------------
        // Recolor: the theme in effect carried over to another background and accent
        // ------------------------------------------------------------------------------

        //! Asks for the two colours and shows the application in the scheme they give while it is open. Accepted: values() is
        //! what the theme takes; rejected: the caller ends the preview.
        class RecolorDialog
            : public QDialog
        {
        public:
            explicit RecolorDialog(QWidget* parent)
                : QDialog(parent)
                , m_before(ThemeTokens::all())
                , m_previewTimer(new QTimer(this))
            {
                setWindowTitle(QObject::tr("Recolor the theme"));
                setMinimumWidth(DialogWidth);
                m_background = ThemeTokens::info(ThemeEditor::BackgroundKey).m_value.value<QColor>();
                m_accent = ThemeTokens::info(ThemeEditor::AccentKey).m_value.value<QColor>();

                auto* layout = new QVBoxLayout(this);
                auto* intro = new QLabel(QObject::tr("Pick the colour of the panels and the accent colour. Every colour of the "
                    "theme follows: the greys take the tint and the lightness of the background, the accent's shades turn with the "
                    "accent. Tune single values afterwards."), this);
                intro->setWordWrap(true);
                layout->addWidget(intro);

                auto* form = new QFormLayout;
                form->addRow(QObject::tr("Background"), colorRow(m_background, QObject::tr("Background")));
                form->addRow(QObject::tr("Accent"), colorRow(m_accent, QObject::tr("Accent")));
                layout->addLayout(form);

                auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
                connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
                connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
                layout->addWidget(buttons);

                m_previewTimer->setSingleShot(true);
                m_previewTimer->setInterval(PreviewDelayMs);
                connect(m_previewTimer, &QTimer::timeout, this, [this]
                {
                    ThemeManager::instance()->preview(values());
                });
            }

            //! The theme as it was when the dialog opened, carried over to the two colours
            QHash<QByteArray, QString> values() const
            {
                return ThemeEditor::recoloredTheme(m_before, m_background, m_accent);
            }

        private:
            //! A swatch and the colour as text, both editing `color`
            QWidget* colorRow(QColor& color, const QString& title)
            {
                auto* row = new QWidget(this);
                auto* layout = new QHBoxLayout(row);
                layout->setContentsMargins(0, 0, 0, 0);
                auto* swatch = new Swatch(row);
                auto* text = new QLineEdit(row);
                text->setFixedWidth(ColorTextWidth);
                layout->addWidget(swatch);
                layout->addWidget(text);
                layout->addStretch(1);

                const auto showColor = [this, &color, swatch, text](const QColor& picked)
                {
                    color = picked;
                    swatch->setColor(picked);
                    if (!text->hasFocus())
                    {
                        text->setText(textOf(picked));
                    }
                    if (!m_previewTimer->isActive())
                    {
                        m_previewTimer->start();
                    }
                };
                swatch->setColor(color);
                text->setText(textOf(color));

                connect(swatch, &QToolButton::clicked, this, [this, &color, showColor, title]
                {
                    const QColor before = color;
                    const QColor picked = pickColorLive(this, title, before, showColor);
                    showColor(picked.isValid() ? picked : before);
                });
                connect(text, &QLineEdit::editingFinished, this, [&color, showColor, text]
                {
                    const QVariant typed = ThemeTokens::fromText(ThemeKind::Color, colorText(text->text()));
                    if (typed.isValid() && typed.value<QColor>() != color)
                    {
                        showColor(typed.value<QColor>());
                    }
                    else
                    {
                        text->setText(textOf(color));
                    }
                });
                return row;
            }

            QVector<ThemeTokens::Info> m_before;
            QColor m_background;
            QColor m_accent;
            QTimer* m_previewTimer = nullptr;
        };
    } // namespace ThemeEditorInternal

    using namespace ThemeEditorInternal;

    // ----------------------------------------------------------------------------------
    // Recolor
    // ----------------------------------------------------------------------------------

    QColor ThemeEditor::recolored(const QColor& color, const QColor& background, const QColor& newBackground, const QColor& accent,
        const QColor& newAccent)
    {
        // The two the scheme is read from come out as the two it is carried to, to the last bit
        if (color.rgb() == background.rgb())
        {
            QColor same = newBackground;
            same.setAlpha(color.alpha());
            return same;
        }
        if (color.rgb() == accent.rgb())
        {
            QColor same = newAccent;
            same.setAlpha(color.alpha());
            return same;
        }

        float hue = 0.0f;
        float saturation = 0.0f;
        float lightness = 0.0f;
        float alpha = 1.0f;
        color.getHslF(&hue, &saturation, &lightness, &alpha);
        // Black, white and what is next to them: text, the tool tip
        if (lightness <= ExtremeLightness || lightness >= 1.0f - ExtremeLightness)
        {
            return color;
        }

        float accentHue = 0.0f;
        float accentSaturation = 0.0f;
        float accentLightness = 0.0f;
        accent.getHslF(&accentHue, &accentSaturation, &accentLightness);
        float newAccentHue = 0.0f;
        float newAccentSaturation = 0.0f;
        float newAccentLightness = 0.0f;
        newAccent.getHslF(&newAccentHue, &newAccentSaturation, &newAccentLightness);

        // QColor gives a grey the hue -1
        const bool chromatic = hue >= 0.0f && saturation > Epsilon;
        if (chromatic && saturation >= AccentSaturation && accentHue >= 0.0f && hueDistance(hue, accentHue) <= AccentHueReach)
        {
            // Of the accent's hue: it turns with the accent and takes its change of saturation and lightness. While the
            // accent stays it stays, to the bit (a way through hue and lightness and back may round a channel).
            if (newAccent.rgb() == accent.rgb())
            {
                return color;
            }
            float turned = newAccentHue >= 0.0f ? hue + (newAccentHue - accentHue) : hue;
            turned -= std::floor(turned);
            const float scaled = accentSaturation > Epsilon ? qBound(0.0f, saturation * newAccentSaturation / accentSaturation, 1.0f)
                                                             : saturation;
            const float shifted = qBound(ExtremeLightness, lightness + (newAccentLightness - accentLightness), 1.0f - ExtremeLightness);
            return QColor::fromHslF(turned, scaled, shifted, alpha);
        }

        if (saturation < AccentSaturation)
        {
            // A grey of the scheme, tinted or not: the new background's tint and its change of lightness. While the
            // background stays it stays, to the bit.
            if (newBackground.rgb() == background.rgb())
            {
                return color;
            }
            float backgroundHue = 0.0f;
            float backgroundSaturation = 0.0f;
            float backgroundLightness = 0.0f;
            background.getHslF(&backgroundHue, &backgroundSaturation, &backgroundLightness);
            float newBackgroundHue = 0.0f;
            float newBackgroundSaturation = 0.0f;
            float newBackgroundLightness = 0.0f;
            newBackground.getHslF(&newBackgroundHue, &newBackgroundSaturation, &newBackgroundLightness);

            const float shifted = qBound(ExtremeLightness, lightness + (newBackgroundLightness - backgroundLightness),
                1.0f - ExtremeLightness);
            float tint = 0.0f;
            if (chromatic && backgroundSaturation > Epsilon)
            {
                // The scheme was tinted: each grey keeps its share of the tint
                tint = qBound(0.0f, saturation * newBackgroundSaturation / backgroundSaturation, 1.0f);
            }
            else
            {
                // The scheme was grey: the new tint, thinning out towards white (a tinted light grey reads as a colour)
                const float room = 1.0f - newBackgroundLightness;
                tint = shifted <= newBackgroundLightness || room <= Epsilon ? newBackgroundSaturation
                                                                            : newBackgroundSaturation * (1.0f - shifted) / room;
            }
            if (newBackgroundHue < 0.0f || tint <= Epsilon)
            {
                return QColor::fromHslF(0.0f, 0.0f, shifted, alpha);
            }
            return QColor::fromHslF(newBackgroundHue, tint, shifted, alpha);
        }

        // A colour of its own (an error red, a success green)
        return color;
    }

    QHash<QByteArray, QString> ThemeEditor::recoloredTheme(const QVector<ThemeTokens::Info>& tokens, const QColor& newBackground,
        const QColor& newAccent)
    {
        QColor background;
        QColor accent;
        for (const ThemeTokens::Info& token : tokens)
        {
            if (token.m_key == BackgroundKey)
            {
                background = token.m_value.value<QColor>();
            }
            else if (token.m_key == AccentKey)
            {
                accent = token.m_value.value<QColor>();
            }
        }

        QHash<QByteArray, QString> values;
        if (!background.isValid() || !accent.isValid())
        {
            return values;
        }
        for (const ThemeTokens::Info& token : tokens)
        {
            // A token that follows another is left to follow it
            if (token.m_kind != ThemeKind::Color || follows(token))
            {
                continue;
            }
            const QColor before = token.m_value.value<QColor>();
            const QColor after = recolored(before, background, newBackground, accent, newAccent);
            if (after != before)
            {
                values.insert(token.m_key, textOf(after));
            }
        }
        return values;
    }

    // ----------------------------------------------------------------------------------
    // Building
    // ----------------------------------------------------------------------------------

    ThemeEditor::ThemeEditor(QWidget* parent)
        : QWidget(parent)
        , m_previewTimer(new QTimer(this))
    {
        setObjectName(QStringLiteral("ThemeEditor"));
        setWindowTitle(tr(Title));

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(OuterMargin, OuterMargin, OuterMargin, OuterMargin);
        layout->setSpacing(RowSpacing + 2);

        ThemeManager* manager = ThemeManager::instance();
        if (!manager)
        {
            auto* notice = new QLabel(
                tr("Themes belong to the flattened GUI. Start the application with --disable-stylesheets to use them."), this);
            notice->setWordWrap(true);
            layout->addWidget(notice);
            layout->addStretch(1);
            return;
        }

        buildHeader(layout);
        buildFilters(layout);

        m_scroll = new QScrollArea(this);
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        layout->addWidget(m_scroll, 1);

        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);

        m_previewTimer->setSingleShot(true);
        m_previewTimer->setInterval(PreviewDelayMs);
        connect(m_previewTimer, &QTimer::timeout, this, &ThemeEditor::previewPending);
        connect(manager, &ThemeManager::themesChanged, this, &ThemeEditor::refreshThemes);
        connect(manager, &ThemeManager::themeApplied, this, &ThemeEditor::refreshValues);

        refreshThemes();
        rebuildRows();
    }

    ThemeEditor::~ThemeEditor() = default;

    ThemeEditor* ThemeEditor::showWindow(QWidget* parent)
    {
        static QPointer<ThemeEditor> window;
        if (!window)
        {
            window = new ThemeEditor(parent);
            window->setWindowFlag(Qt::Window);
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->resize(WindowSize);
            // The decoration a window is given when it is shown (WindowDecorationWrapper) sizes a window without a saved
            // geometry to most of the screen, and follows its guest from then on: the editor takes its own size after it
            QTimer::singleShot(0, window, []
            {
                if (window)
                {
                    window->resize(WindowSize);
                }
            });
        }
        window->show();
        window->raise();
        window->activateWindow();
        return window;
    }

    void ThemeEditor::buildHeader(QVBoxLayout* layout)
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(RowSpacing + 2);
        row->addWidget(new QLabel(tr("Theme"), this));

        m_themes = new QComboBox(this);
        m_themes->setObjectName(QStringLiteral("themePicker"));
        m_themes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(m_themes, 1);
        connect(m_themes, &QComboBox::activated, this, [this](int index)
        {
            ThemeManager::instance()->setActiveTheme(m_themes->itemData(index).toString());
        });

        m_recolorButton = new QToolButton(this);
        m_recolorButton->setObjectName(QStringLiteral("themeRecolor"));
        m_recolorButton->setText(tr("Recolor..."));
        m_recolorButton->setToolTip(tr("Carry the whole theme over to another background and accent colour"));
        row->addWidget(m_recolorButton);
        connect(m_recolorButton, &QToolButton::clicked, this, &ThemeEditor::recolorTheme);

        m_menuButton = new QToolButton(this);
        m_menuButton->setObjectName(QStringLiteral("themeMenu"));
        m_menuButton->setText(tr("Manage"));
        m_menuButton->setPopupMode(QToolButton::InstantPopup);
        row->addWidget(m_menuButton);
        buildMenu();
        layout->addLayout(row);

        m_description = new QLabel(this);
        m_description->setWordWrap(true);
        layout->addWidget(m_description);
    }

    void ThemeEditor::buildMenu()
    {
        auto* menu = new QMenu(m_menuButton);
        menu->addAction(tr("Duplicate..."), this, &ThemeEditor::duplicateTheme);
        menu->addAction(tr("Rename..."), this, &ThemeEditor::renameTheme)->setObjectName(QStringLiteral("rename"));
        menu->addAction(tr("Describe..."), this, &ThemeEditor::describeTheme)->setObjectName(QStringLiteral("describe"));
        menu->addAction(tr("Delete"), this, &ThemeEditor::deleteTheme)->setObjectName(QStringLiteral("delete"));
        menu->addSeparator();
        menu->addAction(tr("Import..."), this, &ThemeEditor::importTheme);
        menu->addAction(tr("Export..."), this, &ThemeEditor::exportTheme);
        menu->addSeparator();
        menu->addAction(tr("Reset every value"), this, []
        {
            ThemeManager::instance()->resetAllValues();
        })->setObjectName(QStringLiteral("resetAll"));
        menu->addAction(tr("Open the themes folder"), this, &ThemeEditor::openUserFolder);
        m_menuButton->setMenu(menu);

        // What one of the engine's themes cannot do is greyed out while one is in effect
        connect(menu, &QMenu::aboutToShow, this, [menu]
        {
            const ThemeManager* manager = ThemeManager::instance();
            const bool isUsers = !manager->isBuiltIn(manager->activeTheme());
            for (QAction* action : menu->actions())
            {
                const QString name = action->objectName();
                if (name == QLatin1String("rename") || name == QLatin1String("describe") || name == QLatin1String("delete")
                    || name == QLatin1String("resetAll"))
                {
                    action->setEnabled(isUsers);
                }
            }
        });
    }

    void ThemeEditor::buildFilters(QVBoxLayout* layout)
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(RowSpacing + 2);

        m_parts = new QTabBar(this);
        m_parts->setObjectName(QStringLiteral("themeParts"));
        m_parts->addTab(tr("Colors"));
        m_parts->addTab(tr("Structure"));
        m_parts->setDrawBase(false);
        m_parts->setExpanding(false);
        row->addWidget(m_parts);
        row->addStretch(1);
        connect(m_parts, &QTabBar::currentChanged, this, &ThemeEditor::rebuildRows);

        m_changedOnly = new QToolButton(this);
        m_changedOnly->setObjectName(QStringLiteral("themeChangedOnly"));
        m_changedOnly->setText(tr("Changed"));
        m_changedOnly->setCheckable(true);
        m_changedOnly->setToolTip(tr("Show only the values this theme sets, at every level of detail"));
        row->addWidget(m_changedOnly);
        connect(m_changedOnly, &QToolButton::toggled, this, &ThemeEditor::rebuildRows);

        row->addWidget(new QLabel(tr("Detail"), this));
        m_level = new QComboBox(this);
        m_level->setObjectName(QStringLiteral("themeLevel"));
        m_level->addItem(tr("Basic"), static_cast<int>(ThemeLevel::Basic));
        m_level->addItem(tr("Standard"), static_cast<int>(ThemeLevel::Standard));
        m_level->addItem(tr("Advanced"), static_cast<int>(ThemeLevel::Advanced));
        m_level->setToolTip(tr("Basic: the few values a scheme starts from. Standard: the roles the controls share. "
                               "Advanced: the values single controls and tools have of their own."));
        row->addWidget(m_level);
        connect(m_level, &QComboBox::activated, this, &ThemeEditor::rebuildRows);
        layout->addLayout(row);

        m_search = new QLineEdit(this);
        m_search->setObjectName(QStringLiteral("themeSearch"));
        m_search->setPlaceholderText(tr("Search every value by name"));
        m_search->setClearButtonEnabled(true);
        layout->addWidget(m_search);
        connect(m_search, &QLineEdit::textChanged, this, &ThemeEditor::rebuildRows);
    }

    bool ThemeEditor::shows(const ThemeTokens::Info& token) const
    {
        const bool isColor = token.m_kind == ThemeKind::Color;
        if (isColor != (m_parts->currentIndex() == static_cast<int>(Part::Colors)))
        {
            return false;
        }
        // What the theme sets shows at whatever level it sits: the list is what the theme is
        const bool changedOnly = m_changedOnly->isChecked();
        if (changedOnly && token.m_text.isEmpty())
        {
            return false;
        }
        // A search looks through every level: it is how a value is found without knowing where it sits
        const QString search = m_search->text().trimmed();
        if (!search.isEmpty())
        {
            return token.m_label.contains(search, Qt::CaseInsensitive)
                || QString::fromUtf8(token.m_key).contains(search, Qt::CaseInsensitive)
                || token.m_description.contains(search, Qt::CaseInsensitive);
        }
        return changedOnly || static_cast<int>(token.m_level) <= m_level->currentData().toInt();
    }

    void ThemeEditor::rebuildRows()
    {
        m_rows.clear();
        auto* content = new QWidget(m_scroll);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(0, 0, OuterMargin, 0);
        layout->setSpacing(RowSpacing);

        const QVector<ThemeTokens::Info> all = ThemeTokens::all();
        const QVector<ThemeTokens::Group> groups = ThemeTokens::groups();
        const QString search = m_search->text().trimmed();
        for (const ThemeTokens::Group& group : groups)
        {
            // A search that names a group shows the group whole
            const bool groupNamed = !search.isEmpty() && group.m_title.contains(search, Qt::CaseInsensitive);
            QVector<ThemeTokens::Info> shown;
            for (const ThemeTokens::Info& token : all)
            {
                if (groupOf(token.m_key) != QString::fromUtf8(group.m_key))
                {
                    continue;
                }
                const bool isColor = token.m_kind == ThemeKind::Color;
                const bool inPart = isColor == (m_parts->currentIndex() == static_cast<int>(Part::Colors));
                if (shows(token) || (groupNamed && inPart && (!m_changedOnly->isChecked() || !token.m_text.isEmpty())))
                {
                    shown.append(token);
                }
            }
            if (!shown.isEmpty())
            {
                addGroup(layout, group, shown);
            }
        }
        if (m_rows.isEmpty())
        {
            auto* none = new QLabel(m_changedOnly->isChecked() ? tr("This theme sets no value here: it reads as the theme it is based on.")
                                                                : tr("No value matches."), content);
            none->setWordWrap(true);
            layout->addWidget(none);
        }
        layout->addStretch(1);

        // The scroll area deletes the content it had
        m_scroll->setWidget(content);
        refreshValues();
    }

    void ThemeEditor::addGroup(QVBoxLayout* layout, const ThemeTokens::Group& group, const QVector<ThemeTokens::Info>& tokens)
    {
        QWidget* content = layout->parentWidget();
        const bool open = !m_collapsed.contains(group.m_key);

        auto* title = new QToolButton(content);
        title->setObjectName(QStringLiteral("themeGroup"));
        title->setText(group.m_title);
        title->setToolTip(group.m_description);
        title->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        title->setAutoRaise(true);
        title->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        QFont bold = title->font();
        bold.setBold(true);
        title->setFont(bold);
        layout->addWidget(title);

        auto* body = new QWidget(content);
        auto* grid = new QGridLayout(body);
        grid->setContentsMargins(GroupIndent, 0, 0, RowSpacing);
        grid->setHorizontalSpacing(RowSpacing + 4);
        grid->setVerticalSpacing(RowSpacing);
        grid->setColumnMinimumWidth(0, LabelColumnWidth);
        grid->setColumnStretch(2, 1);
        int line = 0;
        for (const ThemeTokens::Info& token : tokens)
        {
            addRow(grid, line++, token);
        }
        body->setVisible(open);
        layout->addWidget(body);

        // Not a checked button: the style fills a checked tool button with the accent, and a title is no switch
        const QByteArray key = group.m_key;
        connect(title, &QToolButton::clicked, this, [this, title, body, key]
        {
            const bool isOpen = m_collapsed.contains(key);
            title->setArrowType(isOpen ? Qt::DownArrow : Qt::RightArrow);
            body->setVisible(isOpen);
            if (isOpen)
            {
                m_collapsed.remove(key);
            }
            else
            {
                m_collapsed.insert(key);
            }
        });
    }

    void ThemeEditor::addRow(QGridLayout* grid, int line, const ThemeTokens::Info& token)
    {
        QWidget* body = grid->parentWidget();
        const QByteArray key = token.m_key;
        Row row;
        row.m_key = key;
        row.m_kind = token.m_kind;

        row.m_label = new QLabel(token.m_label, body);
        grid->addWidget(row.m_label, line, 0);

        // ---- The editors of the kind
        auto* editors = new QHBoxLayout;
        editors->setSpacing(RowSpacing);
        if (token.m_kind == ThemeKind::Color)
        {
            row.m_swatch = new Swatch(body);
            row.m_swatch->setObjectName(QStringLiteral("swatch:") + QString::fromUtf8(key));
            editors->addWidget(row.m_swatch);
            connect(row.m_swatch, &QToolButton::clicked, this, [this, key]
            {
                pickColor(key);
            });

            row.m_text = new QLineEdit(body);
            row.m_text->setObjectName(QStringLiteral("value:") + QString::fromUtf8(key));
            row.m_text->setFixedWidth(ColorTextWidth);
            row.m_text->setToolTip(tr("#RRGGBB, #RRGGBBAA, or $ and the key of a value to follow ($tier.Low)"));
            editors->addWidget(row.m_text);
            connect(row.m_text, &QLineEdit::editingFinished, this, [this, key]
            {
                rowEdited(key);
            });
        }
        if (token.m_kind == ThemeKind::Text)
        {
            row.m_font = new QFontComboBox(body);
            row.m_font->setObjectName(QStringLiteral("value:") + QString::fromUtf8(key));
            row.m_font->setFixedWidth(FontWidth);
            editors->addWidget(row.m_font);
            connect(row.m_font, &QFontComboBox::currentFontChanged, this, [this, key]
            {
                rowEdited(key);
            });
        }
        const int numbers = token.m_kind == ThemeKind::Int ? 1 : token.m_kind == ThemeKind::Margins ? 4 : 0;
        for (int side = 0; side < numbers; ++side)
        {
            auto* number = new QSpinBox(body);
            // The editor of a length, or of the first side of margins, has the token's key; the other sides their number
            number->setObjectName(QStringLiteral("value:") + QString::fromUtf8(key)
                + (side == 0 ? QString() : QStringLiteral(":%1").arg(side)));
            number->setRange(-NumberLimit, NumberLimit);
            number->setFixedWidth(NumberWidth);
            if (numbers == 4)
            {
                number->setToolTip(tr(MarginSides[side]));
            }
            row.m_numbers[side] = number;
            editors->addWidget(number);
            connect(number, &QSpinBox::valueChanged, this, [this, key]
            {
                rowEdited(key);
            });
        }
        editors->addStretch(1);
        grid->addLayout(editors, line, 1);

        // ---- What it follows, and the way back to the base theme's value
        row.m_follows = new QLabel(body);
        row.m_follows->setEnabled(false);
        grid->addWidget(row.m_follows, line, 2);

        row.m_reset = new QToolButton(body);
        row.m_reset->setObjectName(QStringLiteral("reset:") + QString::fromUtf8(key));
        row.m_reset->setText(tr("Reset"));
        row.m_reset->setAutoRaise(true);
        row.m_reset->setToolTip(tr("Take this theme's own value away: the value of the theme it is based on returns"));
        grid->addWidget(row.m_reset, line, 3);
        connect(row.m_reset, &QToolButton::clicked, this, [key]
        {
            ThemeManager::instance()->resetValue(key);
        });

        m_rows.append(row);
    }

    // ----------------------------------------------------------------------------------
    // Showing what is in effect
    // ----------------------------------------------------------------------------------

    void ThemeEditor::refreshThemes()
    {
        const ThemeManager* manager = ThemeManager::instance();
        const QString active = manager->activeTheme();
        {
            const QSignalBlocker blocker(m_themes);
            m_themes->clear();
            for (const QString& name : manager->themes())
            {
                m_themes->addItem(manager->isBuiltIn(name) ? name : tr("%1  (yours)").arg(name), name);
            }
            m_themes->setCurrentIndex(m_themes->findData(active));
        }

        const ThemeFile theme = manager->theme(active);
        QString description = theme.m_description;
        if (!theme.m_author.isEmpty())
        {
            description += (description.isEmpty() ? QString() : QStringLiteral("  ")) + tr("By %1.").arg(theme.m_author);
        }
        m_description->setText(description);
        m_description->setVisible(!description.isEmpty());
        if (manager->isBuiltIn(active))
        {
            m_themeStatus = tr("%1 ships with the engine. Changing a value makes a theme of your own that is based on it.").arg(active);
        }
        else
        {
            const QString base = theme.m_base.isEmpty() ? QString::fromLatin1(ThemeManager::OriginalName) : theme.m_base;
            m_themeStatus = tr("%1 is yours, based on %2. Changes are saved as you make them.").arg(active, base);
        }
        refreshStatus();
    }

    void ThemeEditor::refreshStatus()
    {
        // What the last change cost: the whole application takes a theme in this time
        const ThemeManager::ApplyTiming timing = ThemeManager::instance()->lastApplyTiming();
        const qint64 totalUs = timing.m_tokensUs + timing.m_rebuildUs + timing.m_polishUs;
        QString text = m_themeStatus;
        if (timing.m_widgets > 0)
        {
            text += QLatin1Char(' ') + tr("Last change: %1 ms over %2 widgets.").arg(totalUs / 1000.0, 0, 'f', 1).arg(timing.m_widgets);
        }
        m_status->setText(text);

        // For a script that drives the editor (Tools/ThemeProof/editor_theme_bench.py)
        setProperty("themeTokensUs", timing.m_tokensUs);
        setProperty("themeRebuildUs", timing.m_rebuildUs);
        setProperty("themePolishUs", timing.m_polishUs);
        setProperty("themeWidgets", timing.m_widgets);
    }

    void ThemeEditor::refreshValues()
    {
        if (!m_scroll)
        {
            return;
        }
        m_refreshing = true;
        for (Row& row : m_rows)
        {
            refreshRow(row, ThemeTokens::info(row.m_key));
        }
        m_refreshing = false;
        refreshStatus();
    }

    void ThemeEditor::refreshRow(Row& row, const ThemeTokens::Info& token)
    {
        const ThemeManager* manager = ThemeManager::instance();
        const QString text = ThemeTokens::toText(token.m_kind, token.m_value);
        switch (token.m_kind)
        {
        case ThemeKind::Color:
            static_cast<Swatch*>(row.m_swatch)->setColor(token.m_value.value<QColor>());
            if (!row.m_text->hasFocus())
            {
                row.m_text->setText(text);
            }
            break;
        case ThemeKind::Text:
            row.m_font->setCurrentFont(QFont(text));
            break;
        case ThemeKind::Int:
            row.m_numbers[0]->setValue(token.m_value.toInt());
            break;
        case ThemeKind::Margins:
            {
                const QMargins margins = token.m_value.value<QMargins>();
                const int sides[4] = { margins.left(), margins.top(), margins.right(), margins.bottom() };
                for (int side = 0; side < 4; ++side)
                {
                    row.m_numbers[side]->setValue(sides[side]);
                }
            }
            break;
        }

        // The token it takes its value from: the one a theme names (`$key`), else the one the catalogue has it follow, and only
        // while no theme gives it a value of its own
        QByteArray followed;
        if (token.m_text.startsWith(QLatin1Char('$')))
        {
            followed = token.m_text.mid(1).toUtf8();
        }
        else if (token.m_text.isEmpty())
        {
            followed = token.m_source;
        }
        const ThemeTokens::Info source = ThemeTokens::info(followed);
        row.m_follows->setText(source.m_key.isEmpty() ? QString() : tr("follows %1").arg(QString::fromUtf8(source.m_key)));

        // A value the theme in effect sets itself reads bold, and has the way back
        const bool own = manager->hasOwnValue(row.m_key);
        QFont font = row.m_label->font();
        if (font.bold() != own)
        {
            font.setBold(own);
            row.m_label->setFont(font);
        }
        row.m_reset->setVisible(own);

        QString tip = token.m_description;
        tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + tr("%1\nO3DE Original: %2").arg(QString::fromUtf8(token.m_key),
            ThemeTokens::toText(token.m_kind, token.m_original));
        row.m_label->setToolTip(tip);
    }

    // ----------------------------------------------------------------------------------
    // Editing
    // ----------------------------------------------------------------------------------

    void ThemeEditor::rowEdited(const QByteArray& key)
    {
        if (m_refreshing)
        {
            return;
        }
        for (const Row& row : m_rows)
        {
            if (row.m_key != key)
            {
                continue;
            }
            QString text;
            switch (row.m_kind)
            {
            case ThemeKind::Color:
                text = colorText(row.m_text->text());
                break;
            case ThemeKind::Text:
                text = row.m_font->currentFont().family();
                break;
            case ThemeKind::Int:
                text = QString::number(row.m_numbers[0]->value());
                break;
            case ThemeKind::Margins:
                text = QStringLiteral("%1 %2 %3 %4").arg(row.m_numbers[0]->value()).arg(row.m_numbers[1]->value())
                    .arg(row.m_numbers[2]->value()).arg(row.m_numbers[3]->value());
                break;
            }
            // Leaving a field without changing it is no edit
            const ThemeTokens::Info token = ThemeTokens::info(key);
            if (text == ThemeTokens::toText(token.m_kind, token.m_value))
            {
                return;
            }
            if (!ThemeManager::instance()->setValue(key, text))
            {
                // Not a value of the token's kind: the editors show what is in effect again
                refreshValues();
            }
            return;
        }
    }

    void ThemeEditor::previewSoon(const QByteArray& key, const QString& text)
    {
        m_previewKey = key;
        m_previewText = text;
        if (!m_previewTimer->isActive())
        {
            m_previewTimer->start();
        }
    }

    void ThemeEditor::previewPending()
    {
        ThemeManager::instance()->preview(QHash<QByteArray, QString>{ { m_previewKey, m_previewText } });
    }

    void ThemeEditor::pickColor(const QByteArray& key)
    {
        ThemeManager* manager = ThemeManager::instance();
        const ThemeTokens::Info before = ThemeTokens::info(key);
        const QColor colorBefore = before.m_value.value<QColor>();

        // While the picker is open the application shows each colour it passes through, and no theme changes: one of the
        // engine's themes stays the theme in effect until a colour is accepted
        const QColor picked = pickColorLive(this, before.m_label, colorBefore, [this, key](const QColor& color)
        {
            previewSoon(key, textOf(color));
        });
        m_previewTimer->stop();
        if (picked.isValid() && textOf(picked) != textOf(colorBefore))
        {
            manager->setValue(key, textOf(picked));
        }
        else
        {
            // Cancelled, or the colour it had: what the picker showed goes
            manager->endPreview();
        }
    }

    void ThemeEditor::recolorTheme()
    {
        ThemeManager* manager = ThemeManager::instance();
        RecolorDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
        {
            const QHash<QByteArray, QString> values = dialog.values();
            if (!values.isEmpty())
            {
                manager->setValues(values);
                return;
            }
        }
        manager->endPreview();
    }

    // ----------------------------------------------------------------------------------
    // The menu
    // ----------------------------------------------------------------------------------

    void ThemeEditor::duplicateTheme()
    {
        ThemeManager* manager = ThemeManager::instance();
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("Duplicate the theme"), tr("Name of the new theme"), QLineEdit::Normal,
            tr("%1 copy").arg(manager->activeTheme()), &accepted);
        if (accepted && !name.trimmed().isEmpty())
        {
            const QString made = manager->duplicate(manager->activeTheme(), name);
            if (!made.isEmpty())
            {
                manager->setActiveTheme(made);
            }
        }
    }

    void ThemeEditor::renameTheme()
    {
        ThemeManager* manager = ThemeManager::instance();
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("Rename the theme"), tr("New name"), QLineEdit::Normal,
            manager->activeTheme(), &accepted);
        if (accepted && !manager->rename(manager->activeTheme(), name))
        {
            QMessageBox::warning(this, tr("Rename the theme"), tr("The name is empty or taken."));
        }
    }

    void ThemeEditor::describeTheme()
    {
        ThemeManager* manager = ThemeManager::instance();
        const ThemeFile theme = manager->theme(manager->activeTheme());

        QDialog dialog(this);
        dialog.setWindowTitle(tr("Describe the theme"));
        dialog.setMinimumWidth(DialogWidth);
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;
        auto* description = new QLineEdit(theme.m_description, &dialog);
        auto* author = new QLineEdit(theme.m_author, &dialog);
        form->addRow(tr("Description"), description);
        form->addRow(tr("Author"), author);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        if (dialog.exec() == QDialog::Accepted)
        {
            manager->setDetails(description->text(), author->text());
        }
    }

    void ThemeEditor::deleteTheme()
    {
        ThemeManager* manager = ThemeManager::instance();
        const QString name = manager->activeTheme();
        if (QMessageBox::question(this, tr("Delete the theme"), tr("Delete your theme %1? Its file is removed.").arg(name))
            == QMessageBox::Yes)
        {
            manager->remove(name);
        }
    }

    void ThemeEditor::importTheme()
    {
        ThemeManager* manager = ThemeManager::instance();
        const QString path = QFileDialog::getOpenFileName(this, tr("Import a theme"), QString(), tr(FileFilter));
        if (path.isEmpty())
        {
            return;
        }
        QString error;
        const QString name = manager->importTheme(path, &error);
        if (name.isEmpty())
        {
            QMessageBox::warning(this, tr("Import a theme"), error);
            return;
        }
        manager->setActiveTheme(name);
    }

    void ThemeEditor::exportTheme()
    {
        const ThemeManager* manager = ThemeManager::instance();
        const QString name = manager->activeTheme();
        const QString path = QFileDialog::getSaveFileName(this, tr("Export the theme"), name + QStringLiteral(".theme.json"),
            tr(FileFilter));
        QString error;
        if (!path.isEmpty() && !manager->exportTheme(name, path, &error))
        {
            QMessageBox::warning(this, tr("Export the theme"), error);
        }
    }

    void ThemeEditor::openUserFolder()
    {
        const QString folder = ThemeManager::instance()->userFolder();
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    }
} // namespace AzQtComponents
