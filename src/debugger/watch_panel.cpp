#include "debugger/watch_panel.h"
#include "core/emulator.h"
#include "memory/mmu.h"
#include "debug/symbol_table.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QString>

WatchPanel::WatchPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    // Button row
    auto* btn_row = new QHBoxLayout();
    btn_row->setSpacing(4);

    auto* add_btn = new QPushButton(tr("Add"), this);
    connect(add_btn, &QPushButton::clicked, this, &WatchPanel::on_add_watch);
    btn_row->addWidget(add_btn);

    auto* edit_btn = new QPushButton(tr("Edit"), this);
    connect(edit_btn, &QPushButton::clicked, this, &WatchPanel::on_edit_watch);
    btn_row->addWidget(edit_btn);

    auto* remove_btn = new QPushButton(tr("Remove"), this);
    connect(remove_btn, &QPushButton::clicked, this, &WatchPanel::remove_selected);
    btn_row->addWidget(remove_btn);

    btn_row->addStretch();
    layout->addLayout(btn_row);

    // Table
    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({tr("Address"), tr("Label"), tr("Type"), tr("Value")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);

    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    table_->setFont(mono);

    layout->addWidget(table_, 1);
}

void WatchPanel::refresh() {
    if (!emulator_) return;

    for (int i = 0; i < static_cast<int>(watches_.size()); ++i) {
        const auto& w = watches_[i];
        const int width = w.type == BYTE ? 1 : (w.type == WORD ? 2 : 4);
        const uint16_t page_offset = w.addr & 0x1FFF;
        std::optional<uint32_t> value;
        if (w.page) {
            value = emulator_->mmu().debug_read_page_value(
                *w.page, page_offset, static_cast<size_t>(width));
        } else {
            uint32_t live_value = 0;
            for (int b = 0; b < width; ++b) {
                const uint8_t byte = emulator_->mmu().read(
                    static_cast<uint16_t>(w.addr + b));
                live_value |= static_cast<uint32_t>(byte) << (b * 8);
            }
            value = live_value;
        }

        QString value_str = "--";
        if (value) {
            if (w.type == BYTE)
                value_str = QString::asprintf("$%02X", *value);
            else if (w.type == WORD)
                value_str = QString::asprintf("$%04X", *value);
            else
                value_str = QString::asprintf("$%08X", *value);
        }
        if (i < table_->rowCount()) {
            auto* item = table_->item(i, 3);
            if (item) item->setText(value_str);
        }
    }
}

void WatchPanel::add_watch(uint16_t addr, const std::string& label, int type,
                           std::optional<uint8_t> page) {
    WatchEntry entry;
    entry.addr = addr;
    entry.page = page;
    entry.label = label;
    entry.type = static_cast<WatchType>(type);
    watches_.push_back(entry);
    update_table();
}

void WatchPanel::remove_selected() {
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(watches_.size())) return;
    watches_.erase(watches_.begin() + row);
    update_table();
}

bool WatchPanel::show_watch_dialog(const QString& title,
                                    SymbolAddress& location,
                                    std::string& label, int& type)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(400);

    auto* form = new QFormLayout(&dlg);

    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setPlaceholderText(
        "e.g. 4000, $C000@2A, or page_qualified_symbol");
    addr_edit->setText(location.page
        ? QString::asprintf("%04X@%02X", location.address, *location.page)
        : QString::asprintf("%04X", location.address));
    form->addRow(tr("Address or symbol:"), addr_edit);

    auto* label_edit = new QLineEdit(&dlg);
    label_edit->setPlaceholderText("(optional)");
    label_edit->setText(QString::fromStdString(label));
    form->addRow(tr("Label:"), label_edit);

    auto* type_combo = new QComboBox(&dlg);
    type_combo->addItem(tr("Byte"));
    type_combo->addItem(tr("Word"));
    type_combo->addItem(tr("Long"));
    type_combo->setCurrentIndex(type);
    form->addRow(tr("Type:"), type_combo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    if (!symbol_table_) return false;
    const QString entered = addr_edit->text().trimmed();
    const auto resolved = symbol_table_->resolve_address(entered.toStdString());
    if (!resolved) {
        QMessageBox::warning(
            this, tr("Invalid Watch Address"),
            tr("'%1' is not a unique symbol or hexadecimal address.")
                .arg(entered));
        return false;
    }
    location = *resolved;

    label = label_edit->text().trimmed().toStdString();
    if (label.empty()) {
        const auto symbol = location.page
            ? symbol_table_->lookup(*location.page, location.address)
            : symbol_table_->lookup(location.address);
        if (symbol) label = *symbol;
    }
    type = type_combo->currentIndex();
    return true;
}

void WatchPanel::on_add_watch() {
    SymbolAddress location;
    std::string label;
    int type = 0;
    if (show_watch_dialog(tr("Add Watch"), location, label, type))
        add_watch(location.address, label, type, location.page);
}

void WatchPanel::on_edit_watch() {
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(watches_.size())) return;

    auto& w = watches_[row];
    SymbolAddress location{w.page, w.addr};
    std::string label = w.label;
    int type = static_cast<int>(w.type);

    if (show_watch_dialog(tr("Edit Watch"), location, label, type)) {
        w.addr = location.address;
        w.page = location.page;
        w.label = label;
        w.type = static_cast<WatchType>(type);
        update_table();
    }
}

void WatchPanel::update_table() {
    table_->setRowCount(static_cast<int>(watches_.size()));

    static const char* type_names[] = {"Byte", "Word", "Long"};

    for (int i = 0; i < static_cast<int>(watches_.size()); ++i) {
        const auto& w = watches_[i];

        const QString address = w.page
            ? QString::asprintf("$%04X @%02X", w.addr, *w.page)
            : QString::asprintf("$%04X", w.addr);
        auto* addr_item = new QTableWidgetItem(address);
        table_->setItem(i, 0, addr_item);

        auto* label_item = new QTableWidgetItem(QString::fromStdString(w.label));
        table_->setItem(i, 1, label_item);

        auto* type_item = new QTableWidgetItem(type_names[w.type]);
        table_->setItem(i, 2, type_item);

        auto* value_item = new QTableWidgetItem("--");
        table_->setItem(i, 3, value_item);
    }

    refresh();
}
