// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QScreen>
#include <QWindow>
#include "common/logging/log.h"
#include "frontend/helpers/dpi_aware_dialog.h"

DPIAwareDialog::DPIAwareDialog(QWidget* parent, int width, int height)
    : QDialog(parent), original_width(width), original_height(height) {}

DPIAwareDialog::~DPIAwareDialog() = default;

void DPIAwareDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (window_handle) {
        return;
    }

    // Initialize window_handle and connections
    window_handle = windowHandle();
    if (!window_handle) {
        return;
    }

#ifdef __APPLE__
    // Note: macOS implements system level virtualization, so there's no need to connect here.
    // but we still need to call SetContentSizes() at least once to set up the UI
    // macOS style has more padding. Make the dialog larger for compensation.
    resize(original_width * 1.25, original_height * 1.25);
    SetContentSizes();
#else
    resized = false;
    connect(window_handle, &QWindow::screenChanged, this, &DPIAwareDialog::OnScreenChanged);
    OnScreenChanged();
#endif
}

#ifndef __APPLE__
void DPIAwareDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    resized = true;
}

void DPIAwareDialog::OnScreenChanged() {
    // Resize according to DPI
    const double scaleX = window_handle->screen()->logicalDotsPerInchX() / 96.0;
    const double scaleY = window_handle->screen()->logicalDotsPerInchY() / 96.0;
    if (resized) {
        const int new_width = static_cast<int>(scaleX * width() / previous_scaleX);
        const int new_height = static_cast<int>(scaleY * height() / previous_scaleY);
        setMinimumSize(0, 0); // Enforce this resize
        resize(new_width, new_height);
    } else {
        const int new_width = static_cast<int>(original_width * scaleX);
        const int new_height = static_cast<int>(original_height * scaleY);
        setMinimumSize(new_width, new_height);
        adjustSize();
        resized = false; // This resize isn't user-initiated
    }

    SetContentSizes(previous_width, previous_height);
    previous_scaleX = scaleX;
    previous_scaleY = scaleY;
    previous_width = width();
    previous_height = height();
}
#endif
