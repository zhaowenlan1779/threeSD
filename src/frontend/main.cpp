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
#include "common/assert.h"
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

bool IsConfigComplete(const Core::Config& config) {
    return IsConfigGood(config) && !config.certs_db_path.empty() &&
           !config.nand_title_db_path.empty() && !config.ticket_db_path.empty() &&
           !config.system_titles_path.empty() && !config.nand_data_path.empty();
}

MainDialog::MainDialog(QWidget* parent)
    : DPIAwareDialog(parent, 640, 256), ui(std::make_unique<Ui::MainDialog>()) {

    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));

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

    QString destination_text{};
    const auto destination = FileUtil::GetUserPathType();
    if (destination == FileUtil::UserPathType::Normal) {
#ifdef __linux__
        destination_text = tr("Non-Flatpak Citra Install");
#else
        destination_text = tr("User-wide Citra Install");
#endif
    } else if (destination == FileUtil::UserPathType::Portable) {
        destination_text = tr("Portable Citra Install");
    } else if (destination == FileUtil::UserPathType::Flatpak) {
        destination_text = tr("Flatpak Citra Install");
    } else {
        UNREACHABLE();
    }
    ui->importDestination->setText(tr("Import Destination: %1").arg(destination_text));

    connect(ui->main, &QTreeWidget::itemSelectionChanged, [this] {
        ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok)
            ->setEnabled(!ui->main->selectedItems().empty());
    });
    connect(ui->main, &QTreeWidget::itemDoubleClicked, [this] {
        if (!ui->main->selectedItems().empty())
            LaunchImportDialog();
    });

    ui->main->setIndentation(4);

    // Set up device watcher
    auto* device_watcher = new QDeviceWatcher(this);
    device_watcher->start();
    connect(device_watcher, &QDeviceWatcher::deviceAdded, this, &MainDialog::LoadPresetConfig);
    connect(device_watcher, &QDeviceWatcher::deviceChanged, this, &MainDialog::LoadPresetConfig);
    connect(device_watcher, &QDeviceWatcher::deviceRemoved, this, &MainDialog::LoadPresetConfig);
}

MainDialog::~MainDialog() = default;

void MainDialog::SetContentSizes(int previous_width, int previous_height) {
    const int current_width = width();
    if (previous_width == 0) { // first time
        ui->main->setColumnWidth(0, 0.3 * current_width);
        ui->main->setColumnWidth(1, 0.4 * current_width);
    } else { // proportionally update column widths
        for (int i : {0, 1}) {
            ui->main->setColumnWidth(i, ui->main->columnWidth(i) * current_width / previous_width);
        }
    }
}

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
            if (!IsConfigGood(list[i])) {
                status = tr("No Configuration Found");
            } else if (list[i].version != Core::CurrentDumperVersion) {
                status = tr("Version Dismatch");
            } else if (!IsConfigComplete(list[i])) {
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
    if (!IsConfigGood(config)) {
        QMessageBox::critical(
            this, tr("Error"),
            tr("Could not load configuration from this SD card. You need to prepare your SD card "
               "before using threeSD.<br>Please check if you have followed the <a "
               "href='https://github.com/zhaowenlan1779/threeSD/wiki/Quickstart-Guide'>"
               "guide</a> correctly."));
        return;
    }

    if (config.version != Core::CurrentDumperVersion) {
        QMessageBox::critical(this, tr("Version Dismatch"),
                              tr("You are using an unsupported version of threeSDumper.<br>Please "
                                 "ensure that you are using the most recent version of both "
                                 "threeSD and threeSDumper and try again."));
        return;
    }

    if (!IsConfigComplete(config)) {
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
