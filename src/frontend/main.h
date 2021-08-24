// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "core/importer.h"
#include "frontend/helpers/dpi_aware_dialog.h"

namespace Ui {
class MainDialog;
}

class MainDialog final : public DPIAwareDialog {
    Q_OBJECT

public:
    explicit MainDialog(QWidget* parent = nullptr);
    ~MainDialog() override;

private:
    void SetContentSizes(int previous_width, int previous_height) override;

    void LoadPresetConfig();
    void LaunchImportDialog();

    std::vector<Core::Config> preset_config_list;
    std::unique_ptr<Ui::MainDialog> ui;
};
