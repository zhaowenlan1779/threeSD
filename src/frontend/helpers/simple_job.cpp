// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include "frontend/helpers/frontend_common.h"
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
    // We need to create the bar ourselves to circumvent an issue caused by modal ProgressDialog's
    // event handling.
    auto* bar = new QProgressBar(widget);
    bar->setRange(0, 100);
    bar->setValue(0);

    auto* dialog = new QProgressDialog(tr("Initializing..."), tr("Cancel"), 0, 0, widget);
    dialog->setBar(bar);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);

    connect(this, &SimpleJob::ProgressUpdated, this, [bar, dialog](u64 current, u64 total) {
        // Try to map total to int range
        // This is equal to ceil(total / INT_MAX)
        const u64 multiplier =
            (total + std::numeric_limits<int>::max() - 1) / std::numeric_limits<int>::max();
        bar->setMaximum(static_cast<int>(total / multiplier));
        bar->setValue(static_cast<int>(current / multiplier));
        dialog->setLabelText(
            tr("%1 / %2").arg(ReadableByteSize(current)).arg(ReadableByteSize(total)));
    });
    connect(this, &SimpleJob::ErrorOccured, this, [widget, dialog] {
        QMessageBox::critical(widget, tr("threeSD"),
                              tr("Operation failed. Please refer to the log."));
        dialog->hide();
    });
    connect(this, &SimpleJob::Completed, this, [dialog] { dialog->setValue(dialog->maximum()); });
    connect(dialog, &QProgressDialog::canceled, this, &SimpleJob::Cancel);

    start();
}
