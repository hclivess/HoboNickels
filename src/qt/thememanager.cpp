// Copyright (c) 2014-2026 The HoboNickels developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "thememanager.h"

#include "util.h" // GetArg

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace
{
// HoboNickels brand accent: the gold of the coin's gear/ring.
const char *kAccentLight = "#c8920f"; // gold, slightly muted for white backgrounds
const char *kAccentDark  = "#e0a82e"; // brighter gold, reads well on gunmetal

QPalette lightPalette()
{
    QPalette p;
    const QColor accent(kAccentLight);
    p.setColor(QPalette::Window,          QColor("#f2f3f5"));
    p.setColor(QPalette::WindowText,      QColor("#1a1c1e"));
    p.setColor(QPalette::Base,            QColor("#ffffff"));
    p.setColor(QPalette::AlternateBase,   QColor("#f2f3f5"));
    p.setColor(QPalette::ToolTipBase,     QColor("#ffffff"));
    p.setColor(QPalette::ToolTipText,     QColor("#1a1c1e"));
    p.setColor(QPalette::Text,            QColor("#1a1c1e"));
    p.setColor(QPalette::Button,          QColor("#f2f3f5"));
    p.setColor(QPalette::ButtonText,      QColor("#1a1c1e"));
    p.setColor(QPalette::BrightText,      QColor("#d11"));
    p.setColor(QPalette::Link,            accent);
    p.setColor(QPalette::Highlight,       accent);
    p.setColor(QPalette::HighlightedText, QColor("#101010"));
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor("#9aa0a6"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#9aa0a6"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#9aa0a6"));
    return p;
}

QPalette darkPalette()
{
    QPalette p;
    const QColor accent(kAccentDark);
    p.setColor(QPalette::Window,          QColor("#2b2d31"));
    p.setColor(QPalette::WindowText,      QColor("#e3e3e6"));
    p.setColor(QPalette::Base,            QColor("#1e1f22"));
    p.setColor(QPalette::AlternateBase,   QColor("#27292d"));
    p.setColor(QPalette::ToolTipBase,     QColor("#1e1f22"));
    p.setColor(QPalette::ToolTipText,     QColor("#e3e3e6"));
    p.setColor(QPalette::Text,            QColor("#e3e3e6"));
    p.setColor(QPalette::Button,          QColor("#2b2d31"));
    p.setColor(QPalette::ButtonText,      QColor("#e3e3e6"));
    p.setColor(QPalette::BrightText,      QColor("#ff6b6b"));
    p.setColor(QPalette::Link,            accent);
    p.setColor(QPalette::Highlight,       accent);
    p.setColor(QPalette::HighlightedText, QColor("#1a1a1a"));
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor("#6b6e73"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6b6e73"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#6b6e73"));
    return p;
}

// Conservative, layout-safe polish: a little padding and rounded corners on
// buttons/inputs, a flat toolbar, themed selection and progress bar. Colours come
// from the palette where possible; only structural/spacing rules and the accent
// are set here so existing Designer .ui layouts are not disturbed.
QString styleSheet(bool dark)
{
    const QString accent = dark ? kAccentDark : kAccentLight;
    const QString border = dark ? "#3a3d42" : "#c8ccd2";
    const QString base   = dark ? "#1e1f22" : "#ffffff";
    const QString panel  = dark ? "#2b2d31" : "#f2f3f5";
    const QString text   = dark ? "#e3e3e6" : "#1a1c1e";
    const QString hover  = dark ? "#34373c" : "#e8eaed";
    const QString onAccent = dark ? "#1a1a1a" : "#101010";

    QString css;
    css += "QToolBar { border: 0; padding: 4px; spacing: 2px; background: " + panel + "; }";
    css += "QToolButton { padding: 6px 10px; border-radius: 6px; color: " + text + "; }";
    css += "QToolButton:hover { background: " + hover + "; }";
    css += "QToolButton:checked { background: " + accent + "; color: " + onAccent + "; }";
    css += "QPushButton { padding: 5px 14px; border: 1px solid " + border + "; border-radius: 6px; background: " + base + "; color: " + text + "; }";
    css += "QPushButton:hover { border-color: " + accent + "; }";
    css += "QPushButton:default { background: " + accent + "; color: " + onAccent + "; border-color: " + accent + "; }";
    css += "QPushButton:disabled { color: " + border + "; }";
    css += "QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox { padding: 4px 6px; border: 1px solid " + border + "; border-radius: 6px; background: " + base + "; color: " + text + "; }";
    css += "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: " + accent + "; }";
    // Styling a spin box via stylesheet makes Qt take over its up/down sub-controls, and
    // without explicit arrows they render blank. Restore proper increment/decrement
    // buttons with arrow images so the amount fields keep working +/- controls.
    css += "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 16px; border-left: 1px solid " + border + "; border-top-right-radius: 6px; }";
    css += "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 16px; border-left: 1px solid " + border + "; border-bottom-right-radius: 6px; }";
    css += "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: " + hover + "; }";
    css += "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/icons/spin_up); width: 9px; height: 6px; }";
    css += "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/icons/spin_down); width: 9px; height: 6px; }";
    css += "QTabBar::tab { padding: 6px 12px; }";
    css += "QTabBar::tab:selected { border-bottom: 2px solid " + accent + "; }";
    css += "QHeaderView::section { padding: 4px; border: 0; border-bottom: 1px solid " + border + "; background: " + panel + "; }";
    css += "QStatusBar { border-top: 1px solid " + border + "; }";
    css += "QProgressBar { border: 1px solid " + border + "; border-radius: 6px; text-align: center; }";
    css += "QProgressBar::chunk { background: " + accent + "; border-radius: 5px; }";
    return css;
}
} // namespace

void ThemeManager::applyHighDpiSettings()
{
    // High-DPI scaling makes the wallet usable on modern 4K / HiDPI displays,
    // where the un-scaled 2014 UI renders tiny. Must precede QApplication.
#if QT_VERSION >= 0x050600
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif
#if QT_VERSION >= 0x050100
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif
}

QString ThemeManager::configuredTheme()
{
    return QString::fromStdString(GetArg("-uitheme", "light")).trimmed().toLower();
}

void ThemeManager::applyTheme(QApplication &app, const QString &name)
{
    const QString theme = name.trimmed().toLower();
    if (theme == "native")
        return; // leave the platform style and palette untouched

    const bool dark = (theme == "dark");

    // Fusion is Qt's built-in, cross-platform modern style; unlike some native
    // styles it fully honours the QPalette, so the colours below take effect on
    // every platform identically.
    if (QStyle *fusion = QStyleFactory::create("Fusion"))
        app.setStyle(fusion);

    app.setPalette(dark ? darkPalette() : lightPalette());
    app.setStyleSheet(styleSheet(dark));
}
