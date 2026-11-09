#pragma once

#include "editor/brackets.h"
#include <QVector>

class Editor;

class CountingHighlighter : public BracketHighlighter
{
public:
    using BracketHighlighter::BracketHighlighter;
    QVector<int> highlightedBlocks;

protected:
    void highlightBlock(const QString &text) override;
};

class BracketTests : public QObject
{
    Q_OBJECT

    static void moveCursor(Editor &editor, int position);

    static QVector<int> highlightedPositions(const Editor &editor);

private slots:
    void matching_data();

    void matching();

    void incrementalMetadata();

    void cursorMovesAndDocumentReplacement();

    void partnerEditsWithoutCursorMovement();

    void splitMergeAndUndoDeletedBlocks();

    void largeDocumentAndDeepNesting();
};
