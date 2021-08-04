// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <utility>
#include <QDialog>
#include "core/file_sys/cia_common.h"

namespace Ui {
class CIABuildDialog;
}

class CIABuildDialog : public QDialog {
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
