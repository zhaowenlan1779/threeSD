// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "frontend/import_job.h"

#include "common/assert.h"

ImportJob::ImportJob(QObject* parent, Core::SDMCImporter& importer_,
                     std::vector<Core::ContentSpecifier> contents_)
    : QThread(parent), importer(importer_), contents(std::move(contents_)) {}

ImportJob::~ImportJob() = default;

void ImportJob::run() {
    u64 size_imported = 0, count = 0;
    for (const auto& content : contents) {
        emit ProgressUpdated(size_imported, count + 1, content);
        if (!importer.ImportContent(content)) {
            emit ErrorOccured(content);
            return;
        }
        count++;
        size_imported += content.maximum_size;

        if (cancelled) {
            break;
        }
    }
    emit Completed();
}

void ImportJob::Cancel() {
    cancelled.store(true);
}
