// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <QDialog>
#include <QPixmap>
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

    void InsertTopLevelItem(const QString& text, QPixmap icon = {});
    // When replace_name and replace_icon are present they are used instead of those in `content`.
    void InsertSecondLevelItem(std::size_t row, const Core::ContentSpecifier& content,
                               std::size_t id, QString replace_name = {},
                               QPixmap replace_icon = {});

    std::unique_ptr<Ui::ImportDialog> ui;

    std::string user_path;
    Core::SDMCImporter importer;
    std::vector<Core::ContentSpecifier> contents;
    u64 total_size = 0;

    // HACK: To tell whether the checkbox state change is a programmatic trigger
    // TODO: Is there a more elegant way of doing the same?
    bool program_trigger = false;

    // Whether the System Archive / System Data warning has been shown
    bool warning_shown = false;

    // TODO: Why this won't work as locals?
    Core::ContentSpecifier current_content = {};
    u64 current_count = 0;
};
