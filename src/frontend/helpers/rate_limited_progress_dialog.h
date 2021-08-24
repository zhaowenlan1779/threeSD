// Copyright 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono>
#include <QProgressDialog>

class RateLimitedProgressDialog : public QProgressDialog {
public:
    explicit RateLimitedProgressDialog(const QString& label_text, const QString& cancel_button_text,
                                       int minimum, int maximum, QWidget* parent = nullptr);
    ~RateLimitedProgressDialog() override;

    void Update(int progress, const QString& label_text);

private:
    std::chrono::steady_clock::time_point last_update_time = std::chrono::steady_clock::now();
    static constexpr auto MinimumInterval = std::chrono::milliseconds{100};
};
