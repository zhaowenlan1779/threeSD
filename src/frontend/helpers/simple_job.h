// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <QThread>
#include "common/common_types.h"
#include "common/progress_callback.h"

/**
 * Lightweight wrapper around QThread, for easy use with progressive jobs.
 */
class SimpleJob : public QThread {
    Q_OBJECT

public:
    using ExecuteFunc = std::function<bool(const Common::ProgressCallback&)>;
    using AbortFunc = std::function<void()>;

    explicit SimpleJob(QObject* parent, ExecuteFunc execute, AbortFunc abort);
    ~SimpleJob() override;

    void run() override;
    void Cancel();

signals:
    void ProgressUpdated(u64 current, u64 total);
    void Completed();
    void ErrorOccured();

private:
    ExecuteFunc execute;
    AbortFunc abort;
    bool canceled{};
};
