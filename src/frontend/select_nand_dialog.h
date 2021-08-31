// Copyright 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include "core/importer.h"

namespace Ui {
class SelectNandDialog;
}

class SelectNandDialog final : public QDialog {
public:
    explicit SelectNandDialog(QWidget* parent, const std::vector<Core::Config::NandConfig>& nands);
    ~SelectNandDialog();

    int GetResult() const;

private:
    std::unique_ptr<Ui::SelectNandDialog> ui;

    const std::vector<Core::Config::NandConfig>& nands;
    int result = 0;
};
