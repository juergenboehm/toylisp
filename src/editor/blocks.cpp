#include "blocks.h"
#include <QStringList>
#include <QVector>

EvaluationBlock locateBlock(const QString &document, int markerPosition) {
    EvaluationBlock result;
    const auto lines = document.split('\n');
    QVector<int> offsets;
    int offset = 0;
    int marker = -1;
    for (int i = 0; i < lines.size(); ++i) {
        offsets.append(offset);
        if (offset == markerPosition) marker = i;
        offset += lines[i].size() + 1;
    }
    if (marker < 0 || !lines[marker].startsWith("==")) {
        result.error = "Invalid evaluation marker";
        return result;
    }
    int start = marker - 1;
    while (start >= 0 && lines[start] != ">>") --start;
    if (start < 0) { result.error = "Missing >> marker"; return result; }
    result.valid = true;
    result.evaluator = lines[marker].mid(2).trimmed();
    result.input = lines.mid(start + 1, marker - start - 1).join('\n');
    result.insertion = offsets[marker] + lines[marker].size();
    result.replacementEnd = result.insertion;
    for (int i = marker + 1; i < lines.size(); ++i) {
        if (lines[i] == ">>") break;
        if (lines[i] == "##") {
            result.replacementEnd = offsets[i] + lines[i].size();
            break;
        }
    }
    return result;
}
int matchingBracket(const QString &text, int position) {
    const QString opens = "([{";
    const QString closing = ")]}";
    if (position < 0 || position >= text.size()) return -1;
    bool forward = opens.contains(text[position]);
    if (!forward && !closing.contains(text[position])) return -1;
    QString stack;
    int step = forward ? 1 : -1;
    for (int i = position; i >= 0 && i < text.size(); i += step) {
        QChar c = text[i];
        const QString &push = forward ? opens : closing;
        const QString &pop = forward ? closing : opens;
        if (push.contains(c)) stack.append(c);
        else if (pop.contains(c)) {
            if (stack.isEmpty() || push.indexOf(stack.back()) != pop.indexOf(c)) return -1;
            stack.chop(1);
            if (stack.isEmpty()) return i;
        }
    }
    return -1;
}
