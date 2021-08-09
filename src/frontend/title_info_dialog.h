// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <QDialog>
#include "core/file_sys/smdh.h"

namespace Core {
struct Config;
struct ContentSpecifier;
class NCCHContainer;
class SDMCImporter;
class TitleMetadata;
} // namespace Core

namespace Ui {
class TitleInfoDialog;
}

class TitleInfoDialog : public QDialog {
    Q_OBJECT

public:
    explicit TitleInfoDialog(QWidget* parent, Core::SDMCImporter& importer,
                             Core::ContentSpecifier specifier);
    ~TitleInfoDialog();

private:
    void LoadInfo();

    void LoadEncryption(Core::NCCHContainer& ncch);
    void LoadIcons();
    void InitializeLanguageComboBox();
    void InitializeChecks(Core::TitleMetadata& tmd);

    void SaveIcon(bool large);
    void UpdateNames();
    void ExecuteContentsCheck();

    std::unique_ptr<Ui::TitleInfoDialog> ui;

    Core::SDMCImporter& importer;
    const Core::ContentSpecifier specifier;
    Core::SMDH smdh{};
    bool contents_check_result = false;
};
