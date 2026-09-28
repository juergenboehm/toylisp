#include "brackets.h"

#include <algorithm>
#include <QTextCursor>
#include <QVarLengthArray>

namespace {
QChar pairedBracket(QChar character)
{
    switch (character.unicode()) {
    case '(':
        return ')';
    case ')':
        return '(';
    case '[':
        return ']';
    case ']':
        return '[';
    case '{':
        return '}';
    case '}':
        return '{';
    default:
        return {};
    }
}

bool isOpeningBracket(QChar character)
{
    return character == '(' || character == '[' || character == '{';
}

const BracketBlockData *bracketData(const QTextBlock &block)
{
    return static_cast<const BracketBlockData *>(block.userData());
}

int matchingBracket(QTextBlock block, int index)
{
    const auto *data = bracketData(block);
    const bool forward = isOpeningBracket(data->brackets.at(index).character);
    const int step = forward ? 1 : -1;
    QVarLengthArray<QChar, 32> expected;

    while (block.isValid()) {
        if (data) {
            for (; index >= 0 && index < data->brackets.size(); index += step) {
                const auto &bracket = data->brackets.at(index);
                if (isOpeningBracket(bracket.character) == forward) {
                    expected.append(pairedBracket(bracket.character));
                } else {
                    if (expected.isEmpty() || expected.last() != bracket.character)
                        return -1;
                    expected.removeLast();
                    if (expected.isEmpty())
                        return block.position() + bracket.position;
                }
            }
        }
        block = forward ? block.next() : block.previous();
        data = block.isValid() ? bracketData(block) : nullptr;
        index = forward || !data ? 0 : data->brackets.size() - 1;
    }
    return -1;
}
} // namespace

BracketHighlighter::BracketHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    // Qt otherwise defers the first pass when attaching to a populated document.
    rehighlight();
}

void BracketHighlighter::highlightBlock(const QString &text)
{
    auto *data = static_cast<BracketBlockData *>(currentBlockUserData());
    if (data)
        data->brackets.clear();

    for (int position = 0; position < text.size(); ++position) {
        const QChar character = text.at(position);
        if (pairedBracket(character).isNull())
            continue;
        if (!data) {
            data = new BracketBlockData;
            setCurrentBlockUserData(data);
        }
        data->brackets.append({character, position});
    }
}

BracketMatch findBracketMatch(const QTextCursor &cursor)
{
    const QTextBlock block = cursor.block();
    const auto *data = block.isValid() ? bracketData(block) : nullptr;
    if (!data || data->brackets.isEmpty())
        return {};

    const int position = cursor.positionInBlock();
    const auto begin = data->brackets.cbegin();
    const auto end = data->brackets.cend();
    auto bracket = std::lower_bound(begin, end, position, [](const BracketInfo &info, int offset) {
        return info.position < offset;
    });
    if (bracket == end || bracket->position != position) {
        if (bracket == begin || (bracket - 1)->position != position - 1)
            return {};
        --bracket;
    }
    return {
        block.position() + bracket->position,
        matchingBracket(block, static_cast<int>(bracket - begin))};
}
