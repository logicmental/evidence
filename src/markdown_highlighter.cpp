#include "markdown_highlighter.h"

#include <QColor>
#include <QRegularExpression>
#include <QTextCharFormat>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    QTextCharFormat heading;
    heading.setForeground(QColor("#a33f2b"));
    heading.setFontWeight(QFont::Bold);
    const auto headingMatch = QRegularExpression("^#{1,6}\\s.*").match(text);
    if (headingMatch.hasMatch())
        setFormat(headingMatch.capturedStart(), headingMatch.capturedLength(), heading);

    QTextCharFormat emphasis;
    emphasis.setForeground(QColor("#245d56"));
    emphasis.setFontWeight(QFont::DemiBold);
    const QRegularExpression emphasisPattern("\\*\\*[^*]+\\*\\*|__[^_]+__");
    auto emphasisMatch = emphasisPattern.globalMatch(text);
    while (emphasisMatch.hasNext()) {
        const auto match = emphasisMatch.next();
        setFormat(match.capturedStart(), match.capturedLength(), emphasis);
    }

    QTextCharFormat code;
    code.setForeground(QColor("#7b542c"));
    code.setBackground(QColor("#f1ece2"));
    const QRegularExpression codePattern("`[^`]+`");
    auto codeMatch = codePattern.globalMatch(text);
    while (codeMatch.hasNext()) {
        const auto match = codeMatch.next();
        setFormat(match.capturedStart(), match.capturedLength(), code);
    }

    QTextCharFormat wikiLink;
    wikiLink.setForeground(QColor("#24736a"));
    wikiLink.setFontUnderline(true);
    const QRegularExpression linkPattern("\\[\\[[^]]+\\]\\]");
    auto linkMatch = linkPattern.globalMatch(text);
    while (linkMatch.hasNext()) {
        const auto match = linkMatch.next();
        setFormat(match.capturedStart(), match.capturedLength(), wikiLink);
    }
}