// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QDesktopWidget>
#include <QMessageBox>
#include <QPixmap>
#include <fmt/format.h>
#include "common/string_util.h"
#include "core/db/title_db.h"
#include "core/file_sys/ncch_container.h"
#include "core/file_sys/title_metadata.h"
#include "core/importer.h"
#include "frontend/helpers/simple_job.h"
#include "frontend/title_info_dialog.h"
#include "ui_title_info_dialog.h"

TitleInfoDialog::TitleInfoDialog(QWidget* parent, const Core::Config& config,
                                 Core::SDMCImporter& importer_, Core::ContentSpecifier specifier_)
    : QDialog(parent), ui(std::make_unique<Ui::TitleInfoDialog>()), importer(importer_),
      specifier(std::move(specifier_)) {

    ui->setupUi(this);

    const double scale = qApp->desktop()->logicalDpiX() / 96.0;
    resize(static_cast<int>(width() * scale), static_cast<int>(height() * scale));

    LoadInfo(config);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &TitleInfoDialog::accept);
}

TitleInfoDialog::~TitleInfoDialog() = default;

void TitleInfoDialog::LoadInfo(const Core::Config& config) {
    Core::TitleMetadata tmd;
    if (!importer.LoadTMD(specifier, tmd)) {
        QMessageBox::warning(this, tr("threeSD"), tr("Could not load title information."));
        reject();
        return;
    }
    ui->versionLineEdit->setText(QString::fromStdString(tmd.GetTitleVersionString()));
    ui->titleIDLineEdit->setText(QStringLiteral("%1").arg(specifier.id, 16, 16, QLatin1Char{'0'}));

    const bool is_nand = specifier.type == Core::ContentType::SystemTitle ||
                         specifier.type == Core::ContentType::SystemApplet;
    const auto physical_path =
        is_nand ? fmt::format("{}{:08x}/{:08x}/content/", config.system_titles_path,
                              (specifier.id >> 32), (specifier.id & 0xFFFFFFFF))
                : fmt::format("{}title/{:08x}/{:08x}/content/", config.sdmc_path,
                              (specifier.id >> 32), (specifier.id & 0xFFFFFFFF));
    const auto boot_content_path =
        fmt::format("{}{:08x}.app", physical_path, tmd.GetBootContentID());
    Core::NCCHContainer ncch;
    if (is_nand) {
        ncch.OpenFile(std::make_shared<FileUtil::IOFile>(boot_content_path, "rb"));
    } else {
        const auto relative_path = boot_content_path.substr(config.sdmc_path.size() - 1);
        ncch.OpenFile(std::make_shared<Core::SDMCFile>(config.sdmc_path, relative_path, "rb"));
    }

    static const std::unordered_map<Core::EncryptionType, const char*> EncryptionTypeMap{{
        {Core::EncryptionType::None, QT_TR_NOOP("None")},
        {Core::EncryptionType::FixedKey, QT_TR_NOOP("FixedKey")},
        {Core::EncryptionType::NCCHSecure1, QT_TR_NOOP("Secure1")},
        {Core::EncryptionType::NCCHSecure2, QT_TR_NOOP("Secure2")},
        {Core::EncryptionType::NCCHSecure3, QT_TR_NOOP("Secure3")},
        {Core::EncryptionType::NCCHSecure4, QT_TR_NOOP("Secure4")},
    }};

    Core::EncryptionType encryption = Core::EncryptionType::None;
    ncch.ReadEncryptionType(encryption);

    bool seed_crypto = false;
    ncch.ReadSeedCrypto(seed_crypto);

    QString encryption_text = tr(EncryptionTypeMap.at(encryption));
    if (seed_crypto) {
        encryption_text.append(tr(" (Seed)"));
    }
    ui->encryptionLineEdit->setText(encryption_text);

    // Checks
    const bool tmd_legit = tmd.ValidateSignature() && tmd.VerifyHashes();
    if (tmd_legit) {
        ui->tmdCheckLabel->setText(tr("Legit"));
    } else {
        ui->tmdCheckLabel->setText(tr("Illegit"));
    }

    if (const auto& ticket_db = importer.GetTicketDB();
        ticket_db && ticket_db->tickets.count(specifier.id)) {

        const bool ticket_legit = ticket_db->tickets.at(specifier.id).ValidateSignature();
        if (ticket_legit) {
            ui->ticketCheckLabel->setText(tr("Legit"));
        } else {
            ui->ticketCheckLabel->setText(tr("Illegit"));
        }
    } else {
        ui->ticketCheckLabel->setText(tr("Missing"));
    }
    connect(ui->contentsCheckButton, &QPushButton::clicked, this,
            &TitleInfoDialog::ExecuteContentsCheck);

    // Load SMDH
    std::vector<u8> smdh_buffer;
    if (!ncch.LoadSectionExeFS("icon", smdh_buffer) || smdh_buffer.size() != sizeof(Core::SMDH) ||
        !Core::IsValidSMDH(smdh_buffer)) {

        LOG_WARNING(Core, "Failed to load SMDH");
        ui->namesGroupBox->setEnabled(false);
        return;
    }

    std::memcpy(&smdh, smdh_buffer.data(), smdh_buffer.size());
    // Load icon
    ui->iconLargeLabel->setPixmap(
        QPixmap::fromImage(QImage(reinterpret_cast<const uchar*>(smdh.GetIcon(true).data()), 48, 48,
                                  QImage::Format::Format_RGB16)));
    ui->iconSmallLabel->setPixmap(
        QPixmap::fromImage(QImage(reinterpret_cast<const uchar*>(smdh.GetIcon(false).data()), 24,
                                  24, QImage::Format::Format_RGB16)));
    // Load names
    InitializeLanguageComboBox();
}

void TitleInfoDialog::InitializeLanguageComboBox() {
    if (!ui->namesGroupBox->isEnabled()) { // SMDH not available
        return;
    }
    // Corresponds to the indices of the languages defined in SMDH
    static constexpr std::array<const char*, 12> LanguageNames{{
        QT_TR_NOOP("Japanese"),
        QT_TR_NOOP("English"),
        QT_TR_NOOP("French"),
        QT_TR_NOOP("German"),
        QT_TR_NOOP("Italian"),
        QT_TR_NOOP("Spanish"),
        QT_TR_NOOP("Chinese (Simplified)"),
        QT_TR_NOOP("Korean"),
        QT_TR_NOOP("Dutch"),
        QT_TR_NOOP("Portuguese"),
        QT_TR_NOOP("Russian"),
        QT_TR_NOOP("Chinese (Traditional)"),
    }};
    for (std::size_t i = 0; i < LanguageNames.size(); ++i) {
        const auto& title = smdh.titles.at(i);
        if (Common::UTF16BufferToUTF8(title.short_title).empty() &&
            Common::UTF16BufferToUTF8(title.long_title).empty() &&
            Common::UTF16BufferToUTF8(title.publisher).empty()) {
            // All empty, ignore this language
            continue;
        }

        ui->languageComboBox->addItem(tr(LanguageNames.at(i)), static_cast<int>(i));
        if (i == 1) { // English
            ui->languageComboBox->setCurrentIndex(ui->languageComboBox->count() - 1);
        }
    }
    connect(ui->languageComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &TitleInfoDialog::UpdateNames);
    UpdateNames();
}

void TitleInfoDialog::UpdateNames() {
    const auto& title = smdh.titles.at(ui->languageComboBox->currentData().toInt());
    ui->shortTitleLineEdit->setText(
        QString::fromStdString(Common::UTF16BufferToUTF8(title.short_title)));
    ui->longTitleLineEdit->setText(
        QString::fromStdString(Common::UTF16BufferToUTF8(title.long_title)));
    ui->publisherLineEdit->setText(
        QString::fromStdString(Common::UTF16BufferToUTF8(title.publisher)));
}

void TitleInfoDialog::ExecuteContentsCheck() {
    auto* job = new SimpleJob(
        this,
        [this](const Common::ProgressCallback& callback) {
            contents_check_result = importer.CheckTitleContents(specifier, callback);
            return true;
        },
        [this] { importer.AbortImporting(); });
    connect(job, &SimpleJob::Completed, this, [this](bool canceled) {
        if (canceled) {
            return;
        }

        ui->contentsCheckButton->setVisible(false);
        ui->contentsCheckLabel->setVisible(true);
        if (contents_check_result) {
            ui->contentsCheckLabel->setText(tr("OK"));
        } else {
            ui->contentsCheckLabel->setText(tr("Failed"));
        }
    });
    job->StartWithProgressDialog(this);
}
