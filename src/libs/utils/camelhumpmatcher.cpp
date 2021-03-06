/**************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Copyright (C) 2017 BlackBerry Limited <qt@blackberry.com>
** Copyright (C) 2017 Andre Hartmann <aha_1980@gmx.de>
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "camelhumpmatcher.h"

#include <QRegularExpression>
#include <QString>

/**
 * \brief Creates a regexp that represents a specified camel hump pattern,
 *        underscores are not included.
 *
 * \param pattern the camel hump pattern
 * \param caseSensitivity case sensitivity used for camel hump matching;
 *                        does not affect wildcard style matching
 * \return the regexp
 */
QRegularExpression CamelHumpMatcher::createCamelHumpRegExp(
        const QString &pattern, CamelHumpMatcher::CaseSensitivity caseSensitivity)
{
    if (pattern.isEmpty())
        return QRegularExpression();

    /*
     * This code builds a regular expression in order to more intelligently match
     * camel-case and underscore names.
     *
     * For any but the first letter, the following replacements are made:
     *   A => [a-z0-9_]*A
     *   a => (?:[a-zA-Z0-9]*_)?a
     *
     * That means any sequence of lower-case or underscore characters can preceed an
     * upper-case character. And any sequence of lower-case or upper case characters -
     * followed by an underscore can preceed a lower-case character.
     *
     * Examples: (case sensitive mode)
     *   gAC matches getActionController
     *   gac matches get_action_controller
     *
     * It also implements the fully and first-letter-only case sensitivity.
     */

    QString keyRegExp;
    bool first = true;
    const QChar asterisk = '*';
    const QChar question = '?';
    const QLatin1String uppercaseWordFirst("(?<=\\b|[a-z0-9_])");
    const QLatin1String lowercaseWordFirst("(?<=\\b|[A-Z0-9_])");
    const QLatin1String uppercaseWordContinuation("[a-z0-9_]*");
    const QLatin1String lowercaseWordContinuation("(?:[a-zA-Z0-9]*_)?");
    const QLatin1String upperSnakeWordContinuation("[A-Z0-9]*_");
    keyRegExp += "(?:";
    for (const QChar &c : pattern) {
        if (!c.isLetter()) {
            if (c == question)
                keyRegExp += '.';
            else if (c == asterisk)
                keyRegExp += ".*";
            else
                keyRegExp += '(' + QRegularExpression::escape(c) + ')';
        } else if (caseSensitivity == CaseSensitivity::CaseInsensitive ||
            (caseSensitivity == CaseSensitivity::FirstLetterCaseSensitive && !first)) {

            keyRegExp += "(?:";
            keyRegExp += first ? uppercaseWordFirst : uppercaseWordContinuation;
            keyRegExp += '(' + QRegularExpression::escape(c.toUpper());
            if (first) {
                keyRegExp += '|' + lowercaseWordFirst + QRegularExpression::escape(c.toLower()) + ')';
            } else {
                keyRegExp += ")|" + lowercaseWordContinuation;
                keyRegExp += '(' + QRegularExpression::escape(c.toLower()) + ")|";
                keyRegExp += upperSnakeWordContinuation;
                keyRegExp += '(' + QRegularExpression::escape(c.toUpper()) + ')';
            }
            keyRegExp += ')';
        } else {
            if (!first) {
                if (c.isUpper())
                    keyRegExp += uppercaseWordContinuation;
                else
                    keyRegExp += lowercaseWordContinuation;
            }
            keyRegExp += QRegularExpression::escape(c);
        }

        first = false;
    }
    keyRegExp += ")|(" + QRegularExpression::escape(pattern) + ')';

    return QRegularExpression(keyRegExp);
}

/*!
 * \brief Returns a list of matched character positions and their matched lengths for the
 * given regular expression \a match.
 *
 * The list is minimized by combining adjacent highlighting positions to a single position.
 */
CamelHumpMatcher::HighlightingPositions CamelHumpMatcher::highlightingPositions(
        const QRegularExpressionMatch &match)
{
    HighlightingPositions result;

    for (int i = 1, size = match.capturedTexts().size(); i < size; ++i) {
        // skip unused positions, they can appear because upper- and lowercase
        // checks for one character are done using two capture groups
        if (match.capturedStart(i) < 0)
            continue;

        // check for possible highlighting continuation to keep the list minimal
        if (!result.starts.isEmpty()
                && (result.starts.last() + result.lengths.last() == match.capturedStart(i))) {
            result.lengths.last() += match.capturedLength(i);
        } else {
            // no continuation, append as different chunk
            result.starts.append(match.capturedStart(i));
            result.lengths.append(match.capturedLength(i));
        }
    }

    return result;
}
