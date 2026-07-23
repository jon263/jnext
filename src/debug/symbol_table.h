#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SymbolAddress {
    std::optional<uint8_t> page;
    uint16_t address = 0;

    bool operator==(const SymbolAddress& other) const {
        return page == other.page && address == other.address;
    }
};

struct SymbolDefinition {
    uint16_t address = 0;
    std::string display_name;
    std::vector<std::string> aliases;
    std::optional<uint8_t> page;
};

/// Compiler-neutral symbol names indexed by logical address and optional
/// physical 8K page.
class SymbolTable {
public:
    void replace(std::vector<SymbolDefinition> definitions,
                 std::string loaded_file);
    void merge(std::vector<SymbolDefinition> definitions,
               std::string loaded_file);
    void merge(const SymbolTable& other);
    /// Replace only page-qualified entries, preserving logical MAP/Memory
    /// symbols already loaded for the same program.
    void replace_page_qualified(const SymbolTable& other);

    /// Look up an unqualified logical symbol.
    std::optional<std::string> lookup(uint16_t address) const;
    /// Look up an exact page-qualified symbol, then an unqualified fallback.
    std::optional<std::string> lookup(uint8_t page, uint16_t address) const;

    std::optional<uint16_t> lookup_name(const std::string& name) const;
    std::optional<SymbolAddress> lookup_name_address(
        const std::string& name) const;

    /// Resolve a loaded symbol or a hexadecimal address. Numeric forms are
    /// 4000, $4000 and 0x4000; append @2A to pin physical page 0x2A.
    std::optional<uint16_t> resolve(std::string_view text) const;
    std::optional<SymbolAddress> resolve_address(std::string_view text) const;

    /// Legacy unqualified logical symbols.
    const std::map<uint16_t, std::string>& symbols() const {
        return logical_addr_to_name_;
    }
    void clear();
    bool empty() const { return addr_to_name_.empty(); }
    size_t size() const { return addr_to_name_.size(); }
    const std::string& loaded_file() const { return loaded_file_; }

private:
    static uint32_t address_key(std::optional<uint8_t> page, uint16_t address) {
        constexpr uint32_t UNQUALIFIED_PAGE = 0x100;
        return ((page ? static_cast<uint32_t>(*page) : UNQUALIFIED_PAGE) << 16)
               | address;
    }

    void add_definition(SymbolDefinition definition);

    std::map<uint32_t, std::string> addr_to_name_;
    std::map<uint16_t, std::string> logical_addr_to_name_;
    std::map<std::string, std::vector<SymbolAddress>> name_to_addresses_;
    std::string loaded_file_;
};
