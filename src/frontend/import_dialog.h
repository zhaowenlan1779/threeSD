// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <QDialog>
#include "core/importer.h"

class QTreeWidgetItem;

namespace Ui {
class ImportDialog;
}

class ImportDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportDialog(QWidget* parent, const Core::Config& config);
    ~ImportDialog() override;

private:
    void RelistContent();
    void RepopulateContent();
    void UpdateSizeDisplay();
    void UpdateItemCheckState(QTreeWidgetItem* item);
    std::vector<Core::ContentSpecifier> GetSelectedContentList();
    void StartImporting();

    std::unique_ptr<Ui::ImportDialog> ui;

    std::string user_path;
    Core::SDMCImporter importer;
    std::vector<Core::ContentSpecifier> contents;
    u64 total_size = 0;

    // HACK: To tell whether the checkbox state change is a programmatic trigger
    // TODO: Is there a more elegant way of doing the same?
    bool program_trigger = false;

    // TODO: Why this won't work as locals?
    Core::ContentSpecifier current_content = {};
    u64 current_count = 0;
};
