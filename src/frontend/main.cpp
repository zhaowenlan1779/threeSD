// Copyright 2014 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QStorageInfo>
#include <qdevicewatcher.h>
#include "common/file_util.h"
#include "frontend/import_dialog.h"
#include "frontend/main.h"
#include "ui_main.h"

#ifdef __APPLE__
#include <unistd.h>
#include "common/common_paths.h"
#endif

bool IsConfigGood(const Core::Config& config) {
    return !config.sdmc_path.empty() && !config.user_path.empty() &&
           !config.movable_sed_path.empty() && !config.bootrom_path.empty();
}

MainDialog::MainDialog(QWidget* parent) : QDialog(parent), ui(std::make_unique<Ui::MainDialog>()) {
    ui->setupUi(this);

    setFixedWidth(width());
    ui->buttonBox->button(QDialogButtonBox::StandardButton::Reset)->setText(tr("Refresh"));

    LoadPresetConfig();

    connect(ui->advancedButton, &QPushButton::clicked, [this] {
        if (ui->customGroupBox->isVisible()) {
            HideAdvanced();
        } else {
            ShowAdvanced();
        }
    });

    connect(ui->buttonBox, &QDialogButtonBox::clicked, [this](QAbstractButton* button) {
        if (button == ui->buttonBox->button(QDialogButtonBox::StandardButton::Reset)) {
            LoadPresetConfig();
        } else if (button == ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)) {
            LaunchImportDialog();
        }
    });

    // Field 3: Is file
    const std::array<std::tuple<QLineEdit*, QToolButton*, int>, 7> fields{{
        {ui->sdmcPath, ui->sdmcPathExplore, 0},
        {ui->userPath, ui->userPathExplore, 0},
        {ui->movableSedPath, ui->movableSedExplore, 1},
        {ui->bootrom9Path, ui->bootrom9Explore, 1},
        {ui->safeModeFirmPath, ui->safeModeFirmExplore, 0},
        {ui->seeddbPath, ui->seeddbExplore, 1},
        {ui->secretSectorPath, ui->secretSectorExplore, 1},
    }};

    // TODO: better handling (filter)
    for (auto [field, button, isFile] : fields) {
        connect(button, &QToolButton::clicked, [this, field, isFile] {
            QString path;
            if (isFile) {
                path = QFileDialog::getOpenFileName(this, tr("Select File"));
            } else {
                path = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
            }

            if (!path.isEmpty()) {
                field->setText(path);
            }
        });
    }

    // Set up device watcher
    auto* device_watcher = new QDeviceWatcher(this);
    device_watcher->start();
    connect(device_watcher, &QDeviceWatcher::deviceAdded, this, &MainDialog::LoadPresetConfig);
    connect(device_watcher, &QDeviceWatcher::deviceChanged, this, &MainDialog::LoadPresetConfig);
    connect(device_watcher, &QDeviceWatcher::deviceRemoved, this, &MainDialog::LoadPresetConfig);
}

MainDialog::~MainDialog() = default;

void MainDialog::LoadPresetConfig() {
    ui->configSelect->clear();
    preset_config_list.clear();

    for (const auto& storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady()) {
            continue;
        }

        auto list = Core::LoadPresetConfig(storage.rootPath().toStdString());
        for (std::size_t i = 0; i < list.size(); ++i) {
            preset_config_list.emplace_back(list[i]);
            ui->configSelect->addItem(QString::fromStdString(list[i].sdmc_path));
        }
    }

    if (preset_config_list.empty()) {
        // Clear the text
        ui->sdmcPath->setText(QString{});
        ui->userPath->setText(
            QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir)));
        ui->movableSedPath->setText(QString{});
        ui->bootrom9Path->setText(QString{});
        ui->safeModeFirmPath->setText(QString{});
        ui->seeddbPath->setText(QString{});
        ui->secretSectorPath->setText(QString{});

        ui->advancedButton->setVisible(false);
        ShowAdvanced();
        ui->configSelect->addItem(tr("None"));
        ui->configSelect->setCurrentText(tr("None"));
    } else {
        ui->advancedButton->setVisible(true);
        if (ui->customGroupBox->isVisible()) {
            HideAdvanced();
        }
    }
}

void MainDialog::ShowAdvanced() {
    ui->configSelect->setEnabled(false);
    ui->advancedButton->setText(tr("Hide Custom Config"));
    ui->customGroupBox->setVisible(true);

    setMaximumHeight(1000000);
    adjustSize();

    const int index = ui->configSelect->currentIndex();
    ui->configSelect->addItem(tr("Custom"));
    ui->configSelect->setCurrentText(tr("Custom"));

    if (index == -1) {
        return;
    }

    // Load preset data
    const auto config = preset_config_list[static_cast<u32>(index)];
    ui->sdmcPath->setText(QString::fromStdString(config.sdmc_path));
    ui->userPath->setText(QString::fromStdString(config.user_path));
    ui->movableSedPath->setText(QString::fromStdString(config.movable_sed_path));
    ui->bootrom9Path->setText(QString::fromStdString(config.bootrom_path));
    ui->safeModeFirmPath->setText(QString::fromStdString(config.safe_mode_firm_path));
    ui->seeddbPath->setText(QString::fromStdString(config.seed_db_path));
    ui->secretSectorPath->setText(QString::fromStdString(config.secret_sector_path));
}

void MainDialog::HideAdvanced() {
    ui->configSelect->setEnabled(true);
    ui->advancedButton->setText(tr("Customize..."));
    ui->customGroupBox->setVisible(false);

    LoadPresetConfig();

    setMaximumHeight(130);
    adjustSize();
}

Core::Config MainDialog::GetCurrentConfig() {
    if (ui->customGroupBox->isVisible()) {
        Core::Config config{
            /*sdmc_path*/ ui->sdmcPath->text().toStdString(),
            /*user_path*/ ui->userPath->text().toStdString(),
            /*movable_sed_path*/ ui->movableSedPath->text().toStdString(),
            /*bootrom_path*/ ui->bootrom9Path->text().toStdString(),
            /*safe_mode_firm_path*/ ui->safeModeFirmPath->text().toStdString(),
            /*seed_db_path*/ ui->seeddbPath->text().toStdString(),
            /*secret_sector_path*/ ui->secretSectorPath->text().toStdString(),
        };
        return config;
    } else {
        return preset_config_list[ui->configSelect->currentIndex()];
    }
}

void MainDialog::LaunchImportDialog() {
    const auto& config = GetCurrentConfig();
    if (!IsConfigGood(config)) {
        QMessageBox::critical(this, tr("Incomplete Config"),
                              tr("Your config is missing some of the required fields."));
        return;
    }

    ImportDialog dialog(this, config);
    dialog.exec();
}

int main(int argc, char* argv[]) {
    // Init settings params
    QCoreApplication::setOrganizationName(QStringLiteral("zhaowenlan1779"));
    QCoreApplication::setApplicationName(QStringLiteral("threeSD"));

#ifdef __APPLE__
    std::string bin_path = FileUtil::GetBundleDirectory() + DIR_SEP + "..";
    chdir(bin_path.c_str());
#endif

    QApplication app(argc, argv);

    MainDialog main_dialog;
    main_dialog.show();

    return app.exec();
}
