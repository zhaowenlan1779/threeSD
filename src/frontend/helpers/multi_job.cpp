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

    std::size_t count = 0;
    int eta = -1;

    const auto initial_time = std::chrono::steady_clock::now();
    const auto UpdateETA = [total_size, &eta, initial_time](u64 size_imported) {
        if (size_imported < 10 * 1024 * 1024) { // 10M Threshold
            return;
        }
        using namespace std::chrono;
        const u64 time_elapsed =
            duration_cast<milliseconds>(steady_clock::now() - initial_time).count();
        eta =
            static_cast<int>(time_elapsed * (total_size - size_imported) / (size_imported) / 1000);
    };
    const auto Callback = [this, &eta, &UpdateETA](u64 current_imported_size,
                                                   u64 total_imported_size, u64 /*total_size*/) {
        UpdateETA(total_imported_size);
        emit ProgressUpdated(current_imported_size, total_imported_size, eta);
    };

    Common::ProgressCallbackWrapper wrapper{total_size};
    for (const auto& content : contents) {
        emit NextContent(count + 1, wrapper.current_done_size + wrapper.current_pending_size,
                         content, eta);
        if (!execute_func(importer, content, wrapper.Wrap(Callback))) {
            if (!cancelled) {
                failed_contents.emplace_back(content);
            }
        }
        count++;

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
