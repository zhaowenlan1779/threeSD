// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "frontend/helpers/import_job.h"

ImportJob::ImportJob(QObject* parent, Core::SDMCImporter& importer_,
                     std::vector<Core::ContentSpecifier> contents_)
    : QThread(parent), importer(importer_), contents(std::move(contents_)) {}

ImportJob::~ImportJob() = default;

void ImportJob::run() {
    u64 size_imported = 0, count = 0;
    for (const auto& content : contents) {
        emit NextContent(size_imported, count + 1, content);
        const auto callback = [this, size_imported](std::size_t current_size,
                                                    std::size_t /*total_size*/) {
            emit ProgressUpdated(size_imported + current_size, current_size);
        };
        if (!importer.ImportContent(content, callback)) {
            importer.DeleteContent(content);
            if (!cancelled) {
                emit ErrorOccured(content);
                return;
            }
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
    importer.AbortImporting();
}
