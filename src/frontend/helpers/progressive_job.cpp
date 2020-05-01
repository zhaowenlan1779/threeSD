// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "frontend/helpers/progressive_job.h"

ProgressiveJob::ProgressiveJob(QObject* parent, const ExecuteFunc& execute_,
                               const AbortFunc& abort_)
    : QThread(parent), execute(execute_), abort(abort_) {}

ProgressiveJob::~ProgressiveJob() = default;

void ProgressiveJob::run() {
    const bool ret = execute(
        [this](std::size_t current, std::size_t total) { emit ProgressUpdated(current, total); });

    if (ret || canceled) {
        emit Completed();
    } else {
        emit ErrorOccured();
    }
}

void ProgressiveJob::Cancel() {
    canceled = true;
    abort();
}
