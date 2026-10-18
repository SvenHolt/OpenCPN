/**************************************************************************
 *   Copyright (C) 2026 by the OpenCPN development team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

/**
 * \file
 *
 * Stylesheet of the HTML exports (logbook), the same look as the Qt6
 * frontend (qt/src/ui/Theme.cpp) and the seachart harbour guide
 * (tools/seachart/seachart/guide.py): glass cards, night colors, day
 * colors when the system uses a light theme.
 */

#ifndef SEACHART_HTML_STYLE_H_
#define SEACHART_HTML_STYLE_H_

namespace seachart {

inline constexpr const char *kHtmlStyle =
    ":root{--bg:#0a0f15;--panel:rgba(20,23,28,.94);"
    "--border:rgba(255,255,255,.07);--hover:rgba(255,255,255,.06);"
    "--text:#f3f5f8;--dim:#9aa3ae;--faint:#5b636d;--accent:#3e6ae1;"
    "--accent2:#5d8bff;--shadow:rgba(0,0,0,.35)}"
    "@media (prefers-color-scheme:light){:root{--bg:#d9e4ee;--panel:#fff;"
    "--border:rgba(0,0,0,.06);--hover:rgba(0,0,0,.04);--text:#14171c;"
    "--dim:#5b6470;--faint:#9aa1aa;--accent2:#2f5bd3;"
    "--shadow:rgba(20,30,50,.12)}}"
    "body{font:14px Inter,'SF Pro Display','Segoe UI Variable','Segoe UI',"
    "'Noto Sans',Roboto,'Helvetica Neue',Arial,sans-serif;margin:0;"
    "background:var(--bg);color:var(--text)}"
    "header{max-width:960px;margin:auto;padding:28px 16px 4px}"
    "h1{font-size:30px;font-weight:300;margin:0}"
    "main{max-width:960px;margin:auto;padding:4px 16px 32px}"
    "section{background:var(--panel);border:1px solid var(--border);"
    "border-radius:22px;padding:18px 22px;margin:14px 0;"
    "box-shadow:0 10px 28px var(--shadow)}"
    "h2{margin:0 0 8px;font-size:22px;font-weight:600}"
    "p{line-height:1.5}"
    "pre{color:var(--dim);font-size:12px;overflow:auto}";

}  // namespace seachart

#endif  // SEACHART_HTML_STYLE_H_
