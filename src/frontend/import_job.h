// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <QThread>
#include "core/importer.h"

class ImportJob : public QThread {
    Q_OBJECT

public:
    explicit ImportJob(QObject* parent, Core::SDMCImporter& importer,
                       std::vector<Core::ContentSpecifier> contents);
    ~ImportJob() override;

    void run() override;
    void Cancel();

signals:
    void ProgressUpdated(u64 size_imported, u64 count, Core::ContentSpecifier next_content);
    void Completed();
    void ErrorOccured(Core::ContentSpecifier current_content);

private:
    std::atomic_bool cancelled{false};
    Core::SDMCImporter& importer;
    std::vector<Core::ContentSpecifier> contents;
};

Q_DECLARE_METATYPE(Core::ContentSpecifier)
