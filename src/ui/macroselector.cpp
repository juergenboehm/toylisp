#include "macroselector.h"
#include <QDir>
#include <QSignalBlocker>

MacroSelector::MacroSelector(const QString &directory, QWidget *parent)
    : QComboBox(parent)
    , directory(directory)
{
    setObjectName("macroSelector");
    setToolTip("Select a Python text macro");
    setMinimumContentsLength(15);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    refresh();
}

void MacroSelector::refresh()
{
    const QString selectedPath = currentData().toString();
    {
        const QSignalBlocker blocker(this);
        clear();
        const auto files = QDir(directory).entryInfoList({"*.py"}, QDir::Files, QDir::Name);
        for (const auto &file : files) {
            addItem(file.completeBaseName(), file.absoluteFilePath());
            setItemData(count() - 1, file.absoluteFilePath(), Qt::ToolTipRole);
        }
        const int previous = findData(selectedPath);
        if (previous >= 0)
            setCurrentIndex(previous);
    }
    emit macrosChanged();
}

void MacroSelector::showPopup()
{
    refresh();
    QComboBox::showPopup();
}
