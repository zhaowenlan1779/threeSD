// Copyright 2014 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <regex>
#include <string>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QStorageInfo>
#include <QTreeWidgetItem>
#include <qdevicewatcher.h>
#include "common/file_util.h"
#include "frontend/import_dialog.h"
#include "frontend/main.h"
#include "frontend/utilities.h"
#include "ui_main.h"

#ifdef __APPLE__
#include <unistd.h>
#include "common/common_paths.h"
#endif

#ifdef QT_STATICPLUGIN
#include <QtPlugin>

Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin)
#endif
#endif

bool IsConfigGood(const Core::Config& config) {
    return !config.sdmc_path.empty() && !config.user_path.empty() &&
           !config.movable_sed_path.empty() && !config.bootrom_path.empty();
}

MainDialog::MainDialog(QWidget* parent) : QDialog(parent), ui(std::make_unique<Ui::MainDialog>()) {
    ui->setupUi(this);

    ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)->setEnabled(false);
    ui->buttonBox->button(QDialogButtonBox::StandardButton::Reset)->setText(tr("Refresh"));

    LoadPresetConfig();

    connect(ui->utilitiesButton, &QPushButton::clicked, [this] {
        UtilitiesDialog dialog(this);
        dialog.exec();
    });

    connect(ui->buttonBox, &QDialogButtonBox::clicked, [this](QAbstractButton* button) {
        if (button == ui->buttonBox->button(QDialogButtonBox::StandardButton::Reset)) {
            LoadPresetConfig();
        } else if (button == ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)) {
            LaunchImportDialog();
        }
    });

    connect(ui->main, &QTreeWidget::itemSelectionChanged, [this] {
        ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)
            ->setEnabled(!ui->main->selectedItems().empty());
    });
    connect(ui->main, &QTreeWidget::itemDoubleClicked, [this] {
        if (!ui->main->selectedItems().empty())
            LaunchImportDialog();
    });

    ui->main->setIndentation(4);
    ui->main->setColumnWidth(0, 0.3 * width());
    ui->main->setColumnWidth(1, 0.4 * width());
    ui->main->setColumnWidth(2, 0.2 * width());

    // Set up device watcher
    auto* device_watcher = new QDeviceWatcher(this);
    device_watcher->start();
    connect(device_watcher, &QDeviceWatcher::deviceAdded, this, &MainDialog::LoadPresetConfig);
    connect(device_watcher, &QDeviceWatcher::deviceChanged, this, &MainDialog::LoadPresetConfig);
    connect(device_watcher, &QDeviceWatcher::deviceRemoved, this, &MainDialog::LoadPresetConfig);
}

MainDialog::~MainDialog() = default;

static const std::regex sdmc_path_regex{"(.+)([/\\\\])Nintendo 3DS/([0-9a-f]{32})/([0-9a-f]{32})/"};

void MainDialog::LoadPresetConfig() {
    ui->main->clear();
    preset_config_list.clear();

    for (const auto& storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady()) {
            continue;
        }

        auto list = Core::LoadPresetConfig(storage.rootPath().toStdString());
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (!IsConfigGood(list[i])) {
                return;
            }
            preset_config_list.emplace_back(list[i]);

            QString path = storage.rootPath();
            if (path.endsWith(QLatin1Char{'/'}) || path.endsWith(QLatin1Char{'\\'})) {
                path.remove(path.size() - 1, 1);
            }

            // Get ID0
            QString id0 = tr("Unknown");
            std::smatch match;
            if (std::regex_match(list[i].sdmc_path, match, sdmc_path_regex)) {
                if (match.size() >= 5) {
                    id0 = QString::fromStdString(match[3].str());
                }
            }

            // Get status
            QString status = tr("Good");
            if (list[i].safe_mode_firm_path.empty() || list[i].config_savegame_path.empty() ||
                list[i].system_archives_path.empty()) {

                status = tr("Missing System Files");
            } else if (list[i].seed_db_path.empty()) {
                status = tr("Good, Missing Seeds");
            }

            auto* item = new QTreeWidgetItem{{path, id0, status}};
            ui->main->invisibleRootItem()->addChild(item);
        }
    }

    auto* item = new QTreeWidgetItem{{tr("Browse SD Card Root...")}};
    item->setFirstColumnSpanned(true);
    ui->main->invisibleRootItem()->addChild(item);
}

void MainDialog::LaunchImportDialog() {
    auto* item = ui->main->currentItem();
    if (!item) {
        return;
    }

    Core::Config config;
    const auto index = ui->main->invisibleRootItem()->indexOfChild(item);
    if (index == ui->main->invisibleRootItem()->childCount() - 1) {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Select SD Card Root"));
        if (path.isEmpty()) {
            return;
        }

        const auto& list = Core::LoadPresetConfig(path.toStdString());
        if (list.size() > 1) {
            QMessageBox::information(
                this, tr("threeSD"),
                tr("You have more than one 3DS data on your SD Card.\nthreeSD will "
                   "select the first one for you."));
        } else if (list.empty() || !IsConfigGood(list[0])) {
            QMessageBox::critical(
                this, tr("Error"),
                tr("Could not load configuration.<br>Please check if you have followed the <a "
                   "href='https://github.com/zhaowenlan1779/threeSD/wiki/Quickstart-Guide'>"
                   "guide</a> correctly."));
            return;
        }

        config = list[0];
    } else {
        config = preset_config_list.at(index);
    }

    // Check config integrity
    if (config.safe_mode_firm_path.empty() || config.config_savegame_path.empty() ||
        config.system_archives_path.empty()) {
        QMessageBox::warning(
            this, tr("Warning"),
            tr("Certain system files are missing from your configuration.<br>Some contents "
               "may not be importable, or may not run.<br>Please check if you have followed the <a "
               "href='https://github.com/zhaowenlan1779/threeSD/wiki/Quickstart-Guide'>guide</a> "
               "correctly."));
    } else if (config.seed_db_path.empty()) {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Seed database is missing from your configuration.<br>Your system "
                                "likely does not have any seeds.<br>However, if it does have any, "
                                "imported games using seed encryption may not work."));
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

    QIcon::setThemeSearchPaths(QStringList(QStringLiteral(":/icons/default")));
    QIcon::setThemeName(QStringLiteral(":/icons/default"));

    MainDialog main_dialog;
    main_dialog.show();

    return app.exec();
}
