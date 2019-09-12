// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <QThread>
#include "core/importer.h"

class ImportJob : public QThread {
    Q_OBJECT

public:
    explicit ImportJob(QObject* parent, Core::SDMCImporter& importer,
                       std::vector<Core::ContentSpecifier> contents);
    ~ImportJob() override;

    void run() override;
    void Cancel();

signals:
    /**
     * Called when progress is updated on the current content.
     * @param total_size_imported Total imported size taking all previous contents into
     * consideration.
     * @param current_size_imported Imported size of the current content.
     */
    void ProgressUpdated(u64 total_size_imported, u64 current_size_imported);

    /// Dumping of a content has been finished, go on to the next. Called at start as well.
    void NextContent(u64 size_imported, u64 count, Core::ContentSpecifier next_content);

    void Completed();
    void ErrorOccured(Core::ContentSpecifier current_content);

private:
    std::atomic_bool cancelled{false};
    Core::SDMCImporter& importer;
    std::vector<Core::ContentSpecifier> contents;
};

Q_DECLARE_METATYPE(Core::ContentSpecifier)
