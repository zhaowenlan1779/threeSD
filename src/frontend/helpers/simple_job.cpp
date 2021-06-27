// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "frontend/helpers/simple_job.h"

SimpleJob::SimpleJob(QObject* parent, ExecuteFunc execute_, AbortFunc abort_)
    : QThread(parent), execute(std::move(execute_)), abort(std::move(abort_)) {}

SimpleJob::~SimpleJob() = default;

void SimpleJob::run() {
    const bool ret = execute(
        [this](std::size_t current, std::size_t total) { emit ProgressUpdated(current, total); });

    if (ret || canceled) {
        emit Completed();
    } else {
        emit ErrorOccured();
    }
}

void SimpleJob::Cancel() {
    canceled = true;
    abort();
}
