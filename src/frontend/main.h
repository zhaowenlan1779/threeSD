// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>

namespace Ui {
class MainDialog;
}

class MainDialog : public QDialog {
    Q_OBJECT;

public:
    explicit MainDialog(QWidget* parent = nullptr);
    ~MainDialog() override;

private:
    std::unique_ptr<Ui::MainDialog> ui;
};
