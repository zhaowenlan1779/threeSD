// Copyright 2020 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include "frontend/helpers/dpi_aware_dialog.h"

class QWidget;

namespace Ui {
class UtilitiesDialog;
}

class UtilitiesDialog : public DPIAwareDialog {
    Q_OBJECT

public:
    explicit UtilitiesDialog(QWidget* parent);
    ~UtilitiesDialog() override;

private:
    /**
     * Open a dialog to ask the user for source and destination paths.
     * @return {source, destination}
     */
    std::pair<QString, QString> GetFilePaths(bool source_is_dir, bool destination_is_dir);

    bool LoadSDKeys();

    void ShowProgressDialog(std::function<bool()> operation);

    /**
     * Gets SDMC root, and relative source path.
     * @return {success, sdmc root, relative source path}
     */
    std::tuple<bool, std::string, std::string> GetSDMCRoot(const QString& source);

    void SDDecryptionTool();
    void SaveDataExtractionTool();
    void ExtdataExtractionTool();
    void RomFSExtractionTool();
    void ShowResult();

    bool result = false; /// Result of the last operation.
    std::unique_ptr<Ui::UtilitiesDialog> ui;
};
