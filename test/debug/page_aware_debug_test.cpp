#include "debug/sld_loader.h"
#include "debug/source_map.h"
#include "debug/symbol_table.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "memory/rom.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

namespace fs = std::filesystem;

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

} // namespace

int main()
{
    SymbolTable symbols;
    symbols.replace({
        {0x8000, "logical", {}, std::nullopt},
        {0x8100, "refined", {}, std::nullopt},
        {0x8100, "refined", {}, uint8_t{0x2A}},
        {0xC000, "bank42", {"shared"}, uint8_t{0x2A}},
        {0xC000, "bank43", {"shared"}, uint8_t{0x2B}},
    }, "fixture");

    check("page 42 resolves its exact label",
          symbols.lookup(0x2A, 0xC000)
              == std::optional<std::string>("bank42"));
    check("page 43 resolves its exact label",
          symbols.lookup(0x2B, 0xC000)
              == std::optional<std::string>("bank43"));
    check("qualified lookup falls back to a logical label",
          symbols.lookup(0x55, 0x8000)
              == std::optional<std::string>("logical"));
    check("ambiguous banked alias is rejected",
          !symbols.resolve_address("shared"));
    check("page suffix disambiguates a banked alias",
          symbols.resolve_address("shared@2A")
              == std::optional<SymbolAddress>({uint8_t{0x2A}, 0xC000}));
    check("numeric page-qualified address resolves",
          symbols.resolve_address("$C000@2B")
              == std::optional<SymbolAddress>({uint8_t{0x2B}, 0xC000}));
    check("page suffix pins an unqualified symbol",
          symbols.resolve_address("logical@2A")
              == std::optional<SymbolAddress>({uint8_t{0x2A}, 0x8000}));
    check("page-qualified symbol refines an identical logical symbol",
          symbols.resolve_address("refined")
              == std::optional<SymbolAddress>({uint8_t{0x2A}, 0x8100}));
    check("legacy logical resolve remains compatible",
          symbols.resolve("logical") == 0x8000);

    const auto stamp = std::chrono::high_resolution_clock::now()
                           .time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path()
                         / ("jnext-page-aware-debug-" + std::to_string(stamp));
    fs::create_directories(dir);
    const fs::path sld = dir / "banked.sld";
    {
        std::ofstream out(sld);
        out << "|SLD.data.version|1\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,"
               "slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,"
               "49152,57344\n"
            << "main.bas|1||0|42|49152|T|\n"
            << "main.bas|2||0|-1|40960|T|\n"
            << "main.bas|1||0|42|49152|L|,bank42,\n"
            << "main.bas|1||0|43|49152|L|,bank43,\n"
            << "main.bas|1||0|42|53248|L|,common,\n"
            << "main.bas|1||0|43|53248|L|,common,\n"
            << "main.bas|2||0|-1|40960|L|,wildcard,\n";
    }

    SourceMap map;
    SymbolTable sld_symbols;
    const auto loaded = load_sld(map, sld.string(), &sld_symbols);
    check("SLD with labels loads", static_cast<bool>(loaded));
    check("SLD reports imported label records", loaded.symbol_count == 5);
    check("SLD imports page 42 label",
          sld_symbols.lookup(0x2A, 0xC000)
              == std::optional<std::string>("bank42"));
    check("SLD imports page 43 label",
          sld_symbols.lookup(0x2B, 0xC000)
              == std::optional<std::string>("bank43"));
    check("SLD duplicate alias is ambiguous",
          !sld_symbols.resolve_address("common"));
    check("SLD alias page suffix disambiguates",
          sld_symbols.resolve_address("common@2B")
              == std::optional<SymbolAddress>({uint8_t{0x2B}, 0xD000}));
    const auto wildcard_trace = map.lookup(0x55, 0xA000);
    check("SLD page-less trace is a logical wildcard",
          wildcard_trace && wildcard_trace->file == "main.bas"
              && wildcard_trace->line == 2 && !wildcard_trace->page
              && wildcard_trace->address == 0xA000);
    check("SLD page-less label is a logical fallback",
          sld_symbols.lookup(0x55, 0xA000)
              == std::optional<std::string>("wildcard"));
    check("page suffix pins an SLD wildcard label",
          sld_symbols.resolve_address("wildcard@2A")
              == std::optional<SymbolAddress>({uint8_t{0x2A}, 0xA000}));
    SymbolTable replacement;
    replacement.replace({
        {0xD000, "replacement", {}, uint8_t{0x2C}},
    }, "replacement");
    sld_symbols.merge({
        {0x8000, "logical-retained", {}, std::nullopt},
    }, "logical");
    sld_symbols.replace_page_qualified(replacement);
    check("replacing SLD labels removes stale page-qualified symbols",
          !sld_symbols.resolve_address("bank42")
              && sld_symbols.resolve_address("replacement@2C"));
    check("replacing SLD labels preserves logical symbols",
          sld_symbols.resolve("logical-retained") == 0x8000);

    const fs::path malformed = dir / "malformed.sld";
    {
        std::ofstream out(malformed);
        out << "|SLD.data.version|1\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,"
               "slots.count:1,slots.adr:0\n"
            << "main.bas|1||0|999|49152|L|,bad,\n";
    }
    const auto old_symbol_count = sld_symbols.size();
    check("malformed SLD label is rejected",
          !load_sld(map, malformed.string(), &sld_symbols));
    check("failed label load retains active source map",
          map.lookup(0x2A, 0xC000).has_value());
    check("failed label load retains active symbols",
          sld_symbols.size() == old_symbol_count
              && sld_symbols.lookup(0x2C, 0xD000)
                     == std::optional<std::string>("replacement"));

    const fs::path invalid_trace = dir / "invalid-trace.sld";
    {
        std::ofstream out(invalid_trace);
        out << "|SLD.data.version|1\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,"
               "slots.count:1,slots.adr:0\n"
            << "main.bas|1||0|224|49152|T|\n";
    }
    check("out-of-range SLD trace page is rejected",
          !load_sld(map, invalid_trace.string(), &sld_symbols));
    check("failed trace load retains wildcard source records",
          map.lookup(0x55, 0xA000).has_value());

    Ram ram;
    Rom rom;
    Mmu mmu(ram, rom);
    mmu.set_rom_in_sram(true);
    ram.page_ptr(mmu.to_sram_page(0x2A))[0x0123] = 0x42;
    ram.page_ptr(mmu.to_sram_page(0x2B))[0x0123] = 0x43;
    const uint8_t live_page = mmu.get_page(6);
    check("debug read observes inactive page 42",
          mmu.debug_read_page(0x2A, 0x0123) == uint8_t{0x42});
    check("debug read distinguishes inactive page 43",
          mmu.debug_read_page(0x2B, 0x0123) == uint8_t{0x43});
    check("debug read does not alter the live MMU",
          mmu.get_page(6) == live_page);
    ram.page_ptr(mmu.to_sram_page(0x2A))[0x01FE] = 0x34;
    ram.page_ptr(mmu.to_sram_page(0x2A))[0x01FF] = 0x12;
    check("pinned watch value is assembled little-endian",
          mmu.debug_read_page_value(0x2A, 0x01FE, 2) == uint32_t{0x1234});
    check("pinned watch value cannot cross an 8K page",
          !mmu.debug_read_page_value(0x2A, 0x1FFF, 2));

    mmu.bank5_vram()[0x0010] = 0xA5;
    mmu.bank5_vram()[0x2010] = 0xB5;
    mmu.bank7_bram()[0x0010] = 0xA7;
    check("debug read uses dedicated bank 5 low storage",
          mmu.debug_read_page(0x0A, 0x0010) == uint8_t{0xA5});
    check("debug read uses dedicated bank 5 high storage",
          mmu.debug_read_page(0x0B, 0x0010) == uint8_t{0xB5});
    check("debug read uses dedicated bank 7 storage",
          mmu.debug_read_page(0x0E, 0x0010) == uint8_t{0xA7});
    check("debug read rejects the ROM page range",
          !mmu.debug_read_page(0xE0, 0));
    check("debug read rejects offsets outside an 8K page",
          !mmu.debug_read_page(0x2A, 0x2000));

    fs::remove_all(dir);
    std::cout << "Total: " << passed + failed << "  Passed: " << passed
              << "  Failed: " << failed << "  Skipped: 0\n";
    return failed == 0 ? 0 : 1;
}
