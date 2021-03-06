// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include "frontend/helpers/multi_job.h"

MultiJob::MultiJob(QObject* parent, Core::SDMCImporter& importer_,
                   std::vector<Core::ContentSpecifier> contents_, ExecuteFunc execute_func_,
                   AbortFunc abort_func_)
    : QThread(parent), importer(importer_), contents(std::move(contents_)),
      execute_func(std::move(execute_func_)), abort_func(abort_func_) {}

MultiJob::~MultiJob() = default;

void MultiJob::run() {
    u64 total_size = 0;
    for (const auto& content : contents) {
        total_size += content.maximum_size;
    }

    u64 size_imported = 0, count = 0;
    int eta = -1;

    const auto initial_time = std::chrono::steady_clock::now();
    const auto UpdateETA = [total_size, &eta, initial_time](u64 size_imported) {
        if (size_imported >= 10 * 1024 * 1024) { // 10M Threshold
            using namespace std::chrono;
            const u64 time_elapsed =
                duration_cast<milliseconds>(steady_clock::now() - initial_time).count();
            eta = static_cast<int>(time_elapsed * (total_size - size_imported) / (size_imported) /
                                   1000);
        }
    };

    for (const auto& content : contents) {
        emit NextContent(size_imported, count + 1, content, eta);
        const auto callback = [this, size_imported, &eta, &UpdateETA](std::size_t current_size,
                                                                      std::size_t /*total_size*/) {
            UpdateETA(size_imported + current_size);
            emit ProgressUpdated(size_imported + current_size, current_size, eta);
        };
        if (!execute_func(importer, content, callback)) {
            if (!cancelled) {
                failed_contents.emplace_back(content);
            }
        }
        count++;
        size_imported += content.maximum_size;
        UpdateETA(size_imported);

        if (cancelled) {
            break;
        }
    }
    emit Completed();
}

void MultiJob::Cancel() {
    cancelled.store(true);
    abort_func(importer);
}

std::vector<Core::ContentSpecifier> MultiJob::GetFailedContents() const {
    return failed_contents;
}
