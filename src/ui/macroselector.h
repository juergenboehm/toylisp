#pragma once

#include <QComboBox>

class MacroSelector : public QComboBox
{
    Q_OBJECT

public:
    explicit MacroSelector(const QString &directory, QWidget *parent = nullptr);

    void refresh();

    void showPopup() override;

signals:
    void macrosChanged();

private:
    QString directory;
};
