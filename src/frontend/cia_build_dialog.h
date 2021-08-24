// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include "core/file_sys/cia_common.h"
#include "frontend/helpers/dpi_aware_dialog.h"

namespace Ui {
class CIABuildDialog;
}

class CIABuildDialog : public DPIAwareDialog {
    Q_OBJECT

public:
    explicit CIABuildDialog(QWidget* parent, bool is_dir, bool is_nand, bool enable_legit,
                            const QString& default_path);
    ~CIABuildDialog();

    std::pair<QString, Core::CIABuildType> GetResults() const;

private:
    std::unique_ptr<Ui::CIABuildDialog> ui;
    bool is_dir;
};
