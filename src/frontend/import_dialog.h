// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <QPixmap>
#include "core/file_sys/ncch_container.h"
#include "core/importer.h"
#include "helpers/dpi_aware_dialog.h"

class AdvancedMenu;
class MultiJob;
class SimpleJob;
class QTreeWidgetItem;

namespace Ui {
class ImportDialog;
}

class ImportDialog final : public DPIAwareDialog {
    Q_OBJECT

public:
    explicit ImportDialog(QWidget* parent, const Core::Config& config);
    ~ImportDialog() override;

private:
    void SetContentSizes(int previous_width, int previous_height) override;

    void RelistContent();
    void RepopulateContent();
    void UpdateSizeDisplay();
    std::vector<Core::ContentSpecifier> GetSelectedContentList();

    void InsertTopLevelItem(const QString& text, QPixmap icon = {});
    // When replace_name and replace_icon are present they are used instead of those in `content`.
    void InsertSecondLevelItem(std::size_t row, const Core::ContentSpecifier& content,
                               std::size_t id, QString replace_name = {},
                               QPixmap replace_icon = {});

    Core::ContentSpecifier SpecifierFromItem(QTreeWidgetItem* item) const;

    void OnContextMenu(const QPoint& point);
    void ShowAdvancedMenu();

    void OnItemChanged(QTreeWidgetItem* item, int column);

    void RunMultiJob(MultiJob* job, std::size_t total_count, u64 total_size);

    void StartImporting();

    void StartDumpingCXISingle(const Core::ContentSpecifier& content);
    QString last_dump_cxi_path; // Used for recording last path in StartDumpingCXISingle
    void StartBatchDumpingCXI();
    QString last_batch_dump_cxi_path; // Used for recording last path in StartBatchDumpingCXI

    void StartBuildingCIASingle(const Core::ContentSpecifier& content);
    QString last_build_cia_path; // Used for recording last path in StartBuildingCIASingle
    void StartBatchBuildingCIA();
    QString last_batch_build_cia_path; // Used for recording last path in StartBatchBuildingCIA

    std::unique_ptr<Ui::ImportDialog> ui;

    std::unique_ptr<Core::SDMCImporter> importer;
    const Core::Config config;

    std::vector<Core::ContentSpecifier> contents;
    u64 total_selected_size = 0;

    // HACK: Block advanced menu trigger once.
    bool block_advanced_menu = false;
    friend class AdvancedMenu;

    // Whether the System Archive / System Data warning has been shown
    bool system_warning_shown = false;
    // Whether the Applets warning has been shown
    bool applet_warning_shown = false;

    // TODO: Why this won't work as locals?
    Core::ContentSpecifier current_content = {};
    std::size_t current_count = 0;
};
