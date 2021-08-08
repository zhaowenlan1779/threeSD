// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <QDialog>
#include "core/file_sys/smdh.h"

namespace Core {
class Config;
class ContentSpecifier;
class SDMCImporter;
class TitleMetadata;
} // namespace Core

namespace Ui {
class TitleInfoDialog;
}

class TitleInfoDialog : public QDialog {
    Q_OBJECT

public:
    explicit TitleInfoDialog(QWidget* parent, const Core::Config& config,
                             Core::SDMCImporter& importer, Core::ContentSpecifier specifier);
    ~TitleInfoDialog();

private:
    void LoadInfo(const Core::Config& config);
    void InitializeLanguageComboBox();
    void UpdateNames();
    void ExecuteContentsCheck();

    std::unique_ptr<Ui::TitleInfoDialog> ui;

    Core::SDMCImporter& importer;
    const Core::ContentSpecifier specifier;
    Core::SMDH smdh{};
    bool contents_check_result = false;
};
