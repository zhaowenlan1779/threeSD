// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include "frontend/helpers/frontend_common.h"
#include "frontend/helpers/rate_limited_progress_dialog.h"
#include "frontend/helpers/simple_job.h"

SimpleJob::SimpleJob(QObject* parent, ExecuteFunc execute_, AbortFunc abort_)
    : QThread(parent), execute(std::move(execute_)), abort(std::move(abort_)) {}

SimpleJob::~SimpleJob() = default;

void SimpleJob::run() {
    const bool ret =
        execute([this](u64 current, u64 total) { emit ProgressUpdated(current, total); });

    if (ret || canceled) {
        emit Completed(canceled);
    } else {
        emit ErrorOccured();
    }
}

void SimpleJob::Cancel() {
    canceled = true;
    abort();
}

void SimpleJob::StartWithProgressDialog(QWidget* widget) {
    auto* dialog = new RateLimitedProgressDialog(tr("Initializing..."), tr("Cancel"), 0, 0, widget);
    connect(this, &SimpleJob::ProgressUpdated, this, [dialog](u64 current, u64 total) {
        if (dialog->wasCanceled()) {
            return;
        }
        // Try to map total to int range
        // This is equal to ceil(total / INT_MAX)
        const u64 multiplier =
            (total + std::numeric_limits<int>::max() - 1) / std::numeric_limits<int>::max();
        dialog->setMaximum(static_cast<int>(total / multiplier));
        dialog->Update(static_cast<int>(current / multiplier),
                       tr("%1 / %2").arg(ReadableByteSize(current), ReadableByteSize(total)));
    });
    connect(this, &SimpleJob::ErrorOccured, this, [widget, dialog] {
        QMessageBox::critical(widget, tr("threeSD"),
                              tr("Operation failed. Please refer to the log."));
        dialog->hide();
    });
    connect(this, &SimpleJob::Completed, dialog, &QProgressDialog::hide);
    connect(dialog, &QProgressDialog::canceled, this, &SimpleJob::Cancel);

    start();
}
