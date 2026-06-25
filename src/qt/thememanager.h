// Copyright (c) 2014-2026 The HoboNickels developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QString>

class QApplication;

// Modern look-and-feel for the wallet GUI. Self-contained (no Q_OBJECT): it just
// enables high-DPI scaling and applies a built-in Qt style (Fusion), a palette and
// a conservative stylesheet, so the wallet no longer renders in the dated 2014-era
// native theme. The accent colour is the gold of the HoboNickels coin.
namespace ThemeManager
{
    // Must be called BEFORE the QApplication is constructed.
    void applyHighDpiSettings();

    // Apply the named theme AFTER the QApplication exists, before windows are shown.
    //   "light"  (default) - clean light Fusion theme, gold accent
    //   "dark"             - gunmetal dark Fusion theme, gold accent
    //   "native"           - leave the platform style/palette untouched
    void applyTheme(QApplication &app, const QString &name);

    // The theme requested via the -uitheme option (defaults to "light"),
    // normalised to lower case. Reads the daemon argument map, so it is valid
    // only after ParseParameters()/ReadConfigFile().
    QString configuredTheme();
}

#endif // THEMEMANAGER_H
