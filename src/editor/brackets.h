#pragma once

#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QVector>

class QTextCursor;

struct BracketInfo
{
    QChar character;
    int position; // UTF-16 offset relative to the block, as used by QTextCursor.
};

class BracketBlockData : public QTextBlockUserData
{
public:
    QVector<BracketInfo> brackets;
};

class BracketHighlighter : public QSyntaxHighlighter
{
public:
    explicit BracketHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;
};

struct BracketMatch
{
    int position = -1; // Absolute document positions; -1 means absent.
    int partner = -1;
};

// Prefer the bracket at the cursor, then the one immediately before it.
// The document must have a BracketHighlighter with up-to-date block data.
BracketMatch findBracketMatch(const QTextCursor &cursor);
