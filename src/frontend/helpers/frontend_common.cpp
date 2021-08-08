// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cmath>
#include <QObject>
#include "frontend/helpers/frontend_common.h"

QString ReadableByteSize(qulonglong size) {
    static const std::array<const char*, 6> units = {QT_TR_NOOP("B"),   QT_TR_NOOP("KiB"),
                                                     QT_TR_NOOP("MiB"), QT_TR_NOOP("GiB"),
                                                     QT_TR_NOOP("TiB"), QT_TR_NOOP("PiB")};
    if (size == 0)
        return QStringLiteral("0");
    int digit_groups = std::min<int>(static_cast<int>(std::log10(size) / std::log10(1024)),
                                     static_cast<int>(units.size()));
    return QStringLiteral("%L1 %2")
        .arg(size / std::pow(1024, digit_groups), 0, 'f', 1)
        .arg(QObject::tr(units[digit_groups], "FrontendCommon"));
}
