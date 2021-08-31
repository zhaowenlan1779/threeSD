// Copyright 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QPushButton>
#include <QRadioButton>
#include "frontend/select_nand_dialog.h"
#include "ui_select_nand_dialog.h"

SelectNandDialog::SelectNandDialog(QWidget* parent,
                                   const std::vector<Core::Config::NandConfig>& nands_)
    : QDialog(parent), ui(std::make_unique<Ui::SelectNandDialog>()), nands(nands_) {

    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));

    // Initialize radio buttons
    for (std::size_t i = 0; i < nands.size(); ++i) {
        const auto& nand = nands[i];

        // TODO: this is currently hardcoded
        QString display_name;
        if (nand.nand_name == Core::SysNANDName) {
            display_name = tr("SysNAND");
        } else {
            const std::string emu_offset = nand.nand_name.substr(Core::EmuNANDPrefix.size());
            display_name = tr("EmuNAND at 0x%1").arg(QString::fromStdString(emu_offset));
        }

        QRadioButton* button = new QRadioButton(display_name);
        connect(button, &QRadioButton::toggled, this, [this, i](bool checked) {
            if (checked) {
                ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
                result = i;
            }
        });
        ui->contentLayout->addWidget(button);
    }

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

SelectNandDialog::~SelectNandDialog() = default;

int SelectNandDialog::GetResult() const {
    return result;
}
