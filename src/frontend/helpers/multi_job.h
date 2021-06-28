// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <functional>
#include <QThread>
#include "common/progress_callback.h"
#include "core/importer.h"

class MultiJob : public QThread {
    Q_OBJECT

public:
    using ExecuteFunc = std::function<bool(Core::SDMCImporter&, const Core::ContentSpecifier&,
                                           const Common::ProgressCallback&)>;
    using DeleteFunc = std::function<void(Core::SDMCImporter&, const Core::ContentSpecifier&)>;
    using AbortFunc = std::function<void(Core::SDMCImporter&)>;

    explicit MultiJob(QObject* parent, Core::SDMCImporter& importer,
                      std::vector<Core::ContentSpecifier> contents, ExecuteFunc execute_func,
                      DeleteFunc delete_func, AbortFunc abort_func);
    ~MultiJob() override;

    void run() override;
    void Cancel();

    std::vector<Core::ContentSpecifier> GetFailedContents() const;

signals:
    /**
     * Called when progress is updated on the current content.
     * @param total_size_imported Total imported size taking all previous contents into
     * consideration.
     * @param current_size_imported Imported size of the current content.
     * @param eta ETA in seconds, 0 when not determined.
     */
    void ProgressUpdated(u64 total_size_imported, u64 current_size_imported, int eta);

    /// Dumping of a content has been finished, go on to the next. Called at start as well.
    void NextContent(u64 size_imported, u64 count, Core::ContentSpecifier next_content, int eta);

    void Completed();

private:
    std::atomic_bool cancelled{false};
    Core::SDMCImporter& importer;
    std::vector<Core::ContentSpecifier> contents;
    std::vector<Core::ContentSpecifier> failed_contents;
    ExecuteFunc execute_func;
    DeleteFunc delete_func;
    AbortFunc abort_func;
};

Q_DECLARE_METATYPE(Core::ContentSpecifier)
