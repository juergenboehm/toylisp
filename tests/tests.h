#pragma once

#include <QObject>
#include <QPoint>
#include <QTemporaryDir>

class Editor;

class Tests : public QObject
{
    Q_OBJECT

    static constexpr qreal normalPointSize = 12.0;
    static constexpr qreal oneNotchPointSize = 12.6;

    QTemporaryDir settingsDirectory;

    static void evaluate(Editor &editor, int occurrence = 0);

    static bool wheel(
        Editor &editor,
        QPoint angleDelta,
        Qt::KeyboardModifiers modifiers = Qt::ControlModifier,
        QPoint pixelDelta = {});

private slots:
    void initTestCase();

    void arithmetic_data();

    void arithmetic();

    void malformed_data();

    void malformed();

    void transactionalEnvironment();

    void blocks_data();

    void blocks();

    void missingStartAndErrors();

    void sharedSessionSurvivesEditor();

    void textZoomWheel();

    void textZoomLimitsAndScrolling();

    void textZoomPreservesEditingState();

    void textZoomPerTabAndResetAction();

    void files();

    void closePromptsAndTabActions();

    void windowAndScreenshot();
};
