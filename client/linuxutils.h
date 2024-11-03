/*
 * SPDX-FileCopyrightText: 2017 Elvis Angelaccio <elvis.angelaccio@kde.org>
 * SPDX-FileCopyrightText: 2020 The Quotient project
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QtCore/QFileInfo>
#include <QtGui/QIcon>

const auto AppId = QStringLiteral("io.github.quotient_im.Quaternion");

inline bool inFlatpak() { return QFileInfo::exists("/.flatpak-info"); }

inline QIcon appIcon()
{
    using Qt::operator""_s;
    return QIcon::fromTheme(inFlatpak() ? AppId : u"quaternion"_s, QIcon(u":/icon.png"_s));
}
