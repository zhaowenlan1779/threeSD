// Copyright 2020 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "frontend/helpers/dpi_aware_dialog.h"

namespace Ui {
class SelectFilesDialog;
}

class SelectFilesDialog : public DPIAwareDialog {
    Q_OBJECT

public:
    explicit SelectFilesDialog(QWidget* parent, bool source_is_dir, bool destination_is_dir);
    ~SelectFilesDialog() override;

    std::pair<QString, QString> GetResults() const;

private:
    std::unique_ptr<Ui::SelectFilesDialog> ui;
    bool source_is_dir;      // Whether Source should be a directory
    bool destination_is_dir; // Whether Destination should be a directory
};
