// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <QThread>
#include "common/common_types.h"

/**
 * Lightweight wrapper around QThread, for easy use with progressive jobs.
 */
class ProgressiveJob : public QThread {
    Q_OBJECT

public:
    using ProgressCallback = std::function<void(std::size_t, std::size_t)>;
    using ExecuteFunc = std::function<bool(const ProgressCallback&)>;
    using AbortFunc = std::function<void()>;

    explicit ProgressiveJob(QObject* parent, const ExecuteFunc& execute, const AbortFunc& abort);
    ~ProgressiveJob() override;

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
