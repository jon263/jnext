#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/symbol_table.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/callstack_panel.h"
#include "debugger/watch_panel.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

#include <iostream>

namespace {

int passed = 0;
int failed = 0;

void check(const char* name, bool condition)
{
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::cout << "FAIL: " << name << '\n';
    }
}

QString cell(QTableWidget* table, int row, int column)
{
    const auto* item = table ? table->item(row, column) : nullptr;
    return item ? item->text() : QString();
}

void submit_address_dialog(const QString& address)
{
    QTimer::singleShot(0, [address]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        const auto edits = dialog->findChildren<QLineEdit*>();
        if (edits.empty()) return;
        edits.front()->setText(address);
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (buttons && buttons->button(QDialogButtonBox::Ok))
            buttons->button(QDialogButtonBox::Ok)->click();
    });
}

void submit_invalid_address_dialog(const QString& address, bool& warning_seen)
{
    QTimer::singleShot(0, [address, &warning_seen]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        const auto edits = dialog->findChildren<QLineEdit*>();
        if (edits.empty()) return;
        edits.front()->setText(address);
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!buttons || !buttons->button(QDialogButtonBox::Ok)) return;

        QTimer::singleShot(0, [&warning_seen]() {
            auto* warning = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            if (!warning) return;
            warning_seen = warning->windowTitle() == "Invalid Watch Address";
            warning->accept();
        });
        buttons->button(QDialogButtonBox::Ok)->click();
    });
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    Emulator emulator;
    EmulatorConfig config;
    config.type = MachineType::ZX48K;
    check("initializes test emulator", emulator.init(config));

    emulator.ram().page_ptr(0x2A)[0x0123] = 0x42;
    emulator.ram().page_ptr(0x2B)[0x0123] = 0x43;
    emulator.ram().page_ptr(0x2A)[0x0124] = 0x44;
    emulator.ram().page_ptr(0x2A)[0x01FE] = 0x34;
    emulator.ram().page_ptr(0x2A)[0x01FF] = 0x12;
    emulator.mmu().set_page(6, 0x2B);

    SymbolTable symbols;
    symbols.replace({
        {0xC123, "bank42", {"shared"}, uint8_t{0x2A}},
        {0xC123, "bank43", {"shared"}, uint8_t{0x2B}},
        {0xC124, "logical", {}, std::nullopt},
    }, "page-watch-test");

    WatchPanel panel(&emulator);
    panel.set_symbol_table(&symbols);
    auto* table = panel.findChild<QTableWidget*>();
    check("watch table is present", table != nullptr);

    panel.add_watch(0xC123, "bank42", 0, uint8_t{0x2A});
    check("pinned watch is added", panel.watch_count() == 1);
    check("pinned address displays its physical page",
          cell(table, 0, 0) == "$C123 @2A");
    check("pinned watch reads inactive page",
          cell(table, 0, 3) == "$42");
    check("watch refresh does not change the live MMU",
          emulator.mmu().get_page(6) == 0x2B);

    panel.add_watch(0xC123, "bank43", 0, uint8_t{0x2B});
    check("same address on another page has another value",
          cell(table, 1, 3) == "$43");

    panel.add_watch(0xC123, "live", 0);
    check("unqualified watch follows the live mapping",
          cell(table, 2, 3) == "$43");

    panel.add_watch(0xC1FE, "word", 1, uint8_t{0x2A});
    check("pinned Word is little-endian",
          cell(table, 3, 3) == "$1234");

    panel.add_watch(0xDFFF, "crossing", 1, uint8_t{0x2A});
    check("cross-page pinned Word is rejected",
          cell(table, 4, 3) == "--");

    submit_address_dialog("shared@2A");
    panel.on_add_watch();
    check("Add Watch accepts a page-qualified symbol",
          panel.watch_count() == 6);
    check("dialog-created watch displays the selected page",
          cell(table, 5, 0) == "$C123 @2A");
    check("dialog-created watch resolves its page-specific label",
          cell(table, 5, 1) == "bank42");
    check("dialog-created watch reads the inactive page",
          cell(table, 5, 3) == "$42");

    submit_address_dialog("logical@2A");
    panel.on_add_watch();
    check("Add Watch pins an unqualified symbol with a page suffix",
          panel.watch_count() == 7);
    check("pinned logical symbol displays its requested page",
          cell(table, 6, 0) == "$C124 @2A");
    check("pinned logical symbol reads the requested inactive page",
          cell(table, 6, 3) == "$44");

    bool invalid_warning_seen = false;
    submit_invalid_address_dialog("not-a-symbol", invalid_warning_seen);
    panel.on_add_watch();
    check("invalid watch expression shows a warning",
          invalid_warning_seen);
    check("invalid watch expression does not add a row",
          panel.watch_count() == 7);

    BreakpointPanel breakpoint_panel(&emulator);
    breakpoint_panel.set_symbol_table(&symbols);
    submit_address_dialog("shared@2A");
    breakpoint_panel.on_add();
    const auto& breakpoints = emulator.debug_state().breakpoints();
    check("Add Breakpoint accepts a page-qualified symbol",
          breakpoints.has_pc_exact(0x2A, 0xC123));
    check("page-qualified breakpoint does not match another page",
          !breakpoints.has_pc_exact(0x2B, 0xC123));

    auto* breakpoint_table = breakpoint_panel.findChild<QTableWidget*>();
    check("breakpoint table displays the selected page",
          cell(breakpoint_table, 0, 1) == "$C123 @2A");
    check("breakpoint table displays the page-specific symbol",
          cell(breakpoint_table, 0, 2) == "bank42");

    emulator.call_stack().restore_frames({
        {0x2B, 0xC100, 0x2A, 0xC123, 0xFF00, CallType::CALL},
    });
    CallStackPanel callstack_panel(&emulator);
    callstack_panel.set_symbol_table(&symbols);
    callstack_panel.set_paused(true);
    callstack_panel.refresh();
    auto* callstack_table = callstack_panel.findChild<QTableWidget*>();
    check("call stack displays the target page",
          cell(callstack_table, 0, 3) == "bank42 @2A");
    check("call stack uses the symbol from the target page",
          !cell(callstack_table, 0, 3).contains("bank43"));

    std::cout << "Total: " << passed + failed << "  Passed: " << passed
              << "  Failed: " << failed << "  Skipped: 0\n";
    return failed == 0 ? 0 : 1;
}
