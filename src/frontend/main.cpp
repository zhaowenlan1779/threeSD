// Copyright 2014 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QApplication>
#include "frontend/main.h"
#include "ui_main.h"

#ifdef __APPLE__
#include <string>
#include <unistd.h>
#include "common/common_paths.h"
#include "common/file_util.h"
#endif

MainDialog::MainDialog(QWidget* parent) : QDialog(parent), ui(std::make_unique<Ui::MainDialog>()) {
    ui->setupUi(this);
}

MainDialog::~MainDialog() = default;

int main(int argc, char* argv[]) {
    // Init settings params
    QCoreApplication::setOrganizationName("zhaowenlan1779");
    QCoreApplication::setApplicationName("threeSD");

#ifdef __APPLE__
    std::string bin_path = FileUtil::GetBundleDirectory() + DIR_SEP + "..";
    chdir(bin_path.c_str());
#endif

    QApplication app(argc, argv);

    MainDialog main_dialog;
    main_dialog.show();

    return app.exec();
}
