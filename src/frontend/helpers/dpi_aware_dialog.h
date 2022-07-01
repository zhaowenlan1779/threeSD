// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QDialog>

class DPIAwareDialog : public QDialog {
public:
    explicit DPIAwareDialog(QWidget* parent, int width, int height);
    ~DPIAwareDialog() override;

protected:
    void showEvent(QShowEvent* event) override;

    // Called with two zeroes to set up content sizes that are relative to dialog size. Also called
    // when screen is changed, to update those sizes.
    virtual void SetContentSizes([[maybe_unused]] int previous_width = 0,
                                 [[maybe_unused]] int previous_height = 0){};

private:
    QWindow* window_handle{};
    const int original_width{};
    const int original_height{};

#ifndef __APPLE__
protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void OnScreenChanged();

    bool resized = false; // whether this dialog has been manually resized
    double prev_scaleX{};
    double prev_scaleY{};
    int prev_width{};
    int prev_height{};
#endif
};
