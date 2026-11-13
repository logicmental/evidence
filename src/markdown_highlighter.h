#pragma once

#include <QSyntaxHighlighter>

class MarkdownHighlighter final : public QSyntaxHighlighter
{
public:
    explicit MarkdownHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;
};