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
