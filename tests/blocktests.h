#pragma once

#include <QObject>

class BlockTests : public QObject
{
    Q_OBJECT

private slots:
    void locating_data();

    void locating();

    void invalidMarkers_data();

    void invalidMarkers();

    void invalidBlock();

    void editsAndUndoRedo();

    void largeSurroundingDocument();
};
