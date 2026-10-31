#include "transliteration.h"
#include <QMap>

namespace {
const QString vowels = "aAiIuUfFxXeEoO";
const QString vowelLetters = QString::fromUtf8("अआइईउऊऋॠऌॡएऐओऔ");
const QString vowelSigns = QString::fromUtf8(" ािीुूृॄॢॣेैोौ");
const QString consonants = "kKgGNcCjJYwWqQRtTdDnpPbBmyrlvSzsh";
const QString consonantLetters = QString::fromUtf8("कखगघङचछजझञटठडढणतथदधनपफबभमयरलवशषसह");
const QString marks = "MH~'";
const QString markLetters = QString::fromUtf8("ंःँऽ");
} // namespace

bool isSlp1Word(const QString &input)
{
    if (input.isEmpty() || input.size() > 128)
        return false;
    bool hasLetter = false;
    for (QChar c : input) {
        if (!vowels.contains(c) && !consonants.contains(c) && !marks.contains(c))
            return false;
        hasLetter = hasLetter || vowels.contains(c) || consonants.contains(c);
    }
    return hasLetter;
}

bool devanagariToSlp1(const QString &input, QString &output, QString &error)
{
    output.clear();
    error.clear();
    const QString word = input.trimmed().normalized(QString::NormalizationForm_C);
    if (word.isEmpty() || word.size() > 128) {
        error = "Enter one Devanagari word (at most 128 characters)";
        return false;
    }
    bool hasLetter = false;
    for (int i = 0; i < word.size(); ++i) {
        const QChar c = word[i];
        int index = consonantLetters.indexOf(c);
        if (index >= 0) {
            hasLetter = true;
            output += consonants[index];
            const QChar next = i + 1 < word.size() ? word[i + 1] : QChar();
            if (next.unicode() == 0x094d) {
                ++i;
                if (i + 1 < word.size()
                    && (word[i + 1].unicode() == 0x200c || word[i + 1].unicode() == 0x200d))
                    ++i;
            } else {
                const int sign = vowelSigns.indexOf(next);
                if (sign > 0) {
                    output += vowels[sign];
                    ++i;
                } else {
                    output += 'a';
                }
            }
        } else if ((index = vowelLetters.indexOf(c)) >= 0) {
            hasLetter = true;
            output += vowels[index];
        } else if ((index = markLetters.indexOf(c)) >= 0 && hasLetter) {
            output += marks[index];
        } else {
            error = "Enter one Devanagari word; spaces, accents and non-Sanskrit letters are "
                    "unsupported";
            output.clear();
            return false;
        }
    }
    return hasLetter;
}

QString slp1ToDevanagari(const QString &input)
{
    QString result;
    bool pendingConsonant = false;
    for (QChar c : input) {
        int index = vowels.indexOf(c);
        if (index >= 0) {
            if (pendingConsonant) {
                if (index > 0)
                    result += vowelSigns[index];
            } else {
                result += vowelLetters[index];
            }
            pendingConsonant = false;
        } else if ((index = consonants.indexOf(c)) >= 0) {
            if (pendingConsonant)
                result += QChar(0x094d);
            result += consonantLetters[index];
            pendingConsonant = true;
        } else if (c == '/' || c == '\\' || c == '^') {
            // Dictionary SLP1 accents do not affect the displayed base spelling.
            continue;
        } else {
            if (pendingConsonant)
                result += QChar(0x094d);
            pendingConsonant = false;
            index = marks.indexOf(c);
            result += index >= 0 ? markLetters[index] : c;
        }
    }
    if (pendingConsonant)
        result += QChar(0x094d);
    return result;
}
