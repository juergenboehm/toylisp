#include "blocks.h"
#include <QStringList>

namespace {
QStringList plainTextLines(const QTextBlock &block)
{
    // Preserve toPlainText() normalization locally. A soft line break does not
    // start a new QTextBlock, but still separates input lines and markers.
    QString text = block.text();
    text.replace(QChar::Nbsp, QLatin1Char(' '));
    return text.split(QChar::LineSeparator);
}
} // namespace

EvaluationBlock locateBlock(const QTextBlock &markerBlock)
{
    EvaluationBlock result;
    if (!markerBlock.isValid() || !markerBlock.text().startsWith("==")) {
        result.error = "Invalid evaluation marker";
        return result;
    }

    QStringList inputLines;
    bool foundStart = false;
    for (auto block = markerBlock.previous(); block.isValid() && !foundStart;
         block = block.previous()) {
        const auto lines = plainTextLines(block);
        for (int i = lines.size() - 1; i >= 0; --i) {
            if (lines[i] == ">>") {
                foundStart = true;
                break;
            }
            inputLines.prepend(lines[i]);
        }
    }
    if (!foundStart) {
        result.error = "Missing >> marker";
        return result;
    }

    const auto markerLines = plainTextLines(markerBlock);
    result.valid = true;
    result.evaluator = markerLines.first().mid(2).trimmed();
    result.input = inputLines.join('\n');
    result.insertion = markerBlock.position() + markerLines.first().size();
    result.replacementEnd = result.insertion;

    for (auto block = markerBlock; block.isValid(); block = block.next()) {
        const auto lines = block == markerBlock ? markerLines : plainTextLines(block);
        int position = block.position();
        for (int i = 0; i < lines.size(); ++i) {
            const auto &line = lines[i];
            if (block != markerBlock || i > 0) {
                if (line == ">>")
                    return result;
                if (line == "##") {
                    result.replacementEnd = position + line.size();
                    return result;
                }
            }
            position += line.size() + 1;
        }
    }
    return result;
}
