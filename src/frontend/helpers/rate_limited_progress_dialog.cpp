// Copyright 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "frontend/helpers/rate_limited_progress_dialog.h"

RateLimitedProgressDialog::RateLimitedProgressDialog(const QString& label_text,
                                                     const QString& cancel_button_text, int minimum,
                                                     int maximum, QWidget* parent)
    : QProgressDialog(label_text, cancel_button_text, minimum, maximum, parent) {

    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    setWindowModality(Qt::WindowModal);
    setMinimumDuration(0);
    setValue(0);
}

RateLimitedProgressDialog::~RateLimitedProgressDialog() = default;

void RateLimitedProgressDialog::Update(int progress, const QString& label_text) {
    if (progress == maximum()) { // always set the maximum
        setValue(progress);
        return;
    }

    const auto current_time = std::chrono::steady_clock::now();
    if (current_time - last_update_time < MinimumInterval) {
        return;
    }

    setValue(progress);
    setLabelText(label_text);
    last_update_time = current_time;
}
