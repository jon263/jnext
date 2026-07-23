#include "debug/symbol_table.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>

namespace {

std::string trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::optional<unsigned int> hex_value(std::string value,
                                      unsigned int maximum)
{
    value = trim(value);
    if (!value.empty() && value.front() == '$') {
        value.erase(value.begin());
    } else if (value.size() > 2 && value[0] == '0'
               && (value[1] == 'x' || value[1] == 'X')) {
        value.erase(0, 2);
    }
    if (value.empty()
        || !std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return std::isxdigit(c) != 0;
           })) {
        return std::nullopt;
    }

    unsigned int parsed = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed, 16);
    if (error != std::errc{} || end != value.data() + value.size()
        || parsed > maximum) {
        return std::nullopt;
    }
    return parsed;
}

} // namespace

void SymbolTable::replace(std::vector<SymbolDefinition> definitions,
                          std::string loaded_file)
{
    clear();
    merge(std::move(definitions), std::move(loaded_file));
}

void SymbolTable::add_definition(SymbolDefinition definition)
{
    if (definition.display_name.empty()) return;
    const SymbolAddress address{definition.page, definition.address};
    addr_to_name_.try_emplace(
        address_key(definition.page, definition.address),
        definition.display_name);
    if (!definition.page) {
        logical_addr_to_name_.try_emplace(
            definition.address, definition.display_name);
    }

    auto add_name = [&](std::string name) {
        if (name.empty()) return;
        auto& addresses = name_to_addresses_[std::move(name)];
        if (std::find(addresses.begin(), addresses.end(), address)
            == addresses.end()) {
            addresses.push_back(address);
        }
    };
    add_name(std::move(definition.display_name));
    for (auto& alias : definition.aliases) add_name(std::move(alias));
}

void SymbolTable::merge(std::vector<SymbolDefinition> definitions,
                        std::string loaded_file)
{
    for (auto& definition : definitions)
        add_definition(std::move(definition));
    loaded_file_ = std::move(loaded_file);
}

void SymbolTable::merge(const SymbolTable& other)
{
    for (const auto& [key, name] : other.addr_to_name_) {
        const uint32_t page_value = key >> 16;
        const SymbolAddress address{
            page_value == 0x100
                ? std::nullopt
                : std::optional<uint8_t>(static_cast<uint8_t>(page_value)),
            static_cast<uint16_t>(key)};
        addr_to_name_.try_emplace(key, name);
        if (!address.page)
            logical_addr_to_name_.try_emplace(address.address, name);
    }
    for (const auto& [name, incoming] : other.name_to_addresses_) {
        auto& addresses = name_to_addresses_[name];
        for (const auto& address : incoming) {
            if (std::find(addresses.begin(), addresses.end(), address)
                == addresses.end()) {
                addresses.push_back(address);
            }
        }
    }
    if (!other.loaded_file_.empty()) loaded_file_ = other.loaded_file_;
}

void SymbolTable::replace_page_qualified(const SymbolTable& other)
{
    for (auto it = addr_to_name_.begin(); it != addr_to_name_.end();) {
        if ((it->first >> 16) != 0x100) {
            it = addr_to_name_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = name_to_addresses_.begin();
         it != name_to_addresses_.end();) {
        auto& addresses = it->second;
        addresses.erase(
            std::remove_if(addresses.begin(), addresses.end(),
                           [](const SymbolAddress& address) {
                               return address.page.has_value();
                           }),
            addresses.end());
        if (addresses.empty()) {
            it = name_to_addresses_.erase(it);
        } else {
            ++it;
        }
    }
    merge(other);
}

std::optional<std::string> SymbolTable::lookup(uint16_t address) const
{
    const auto found = addr_to_name_.find(address_key(std::nullopt, address));
    return found == addr_to_name_.end()
        ? std::nullopt : std::optional<std::string>(found->second);
}

std::optional<std::string> SymbolTable::lookup(uint8_t page,
                                               uint16_t address) const
{
    if (const auto exact = addr_to_name_.find(address_key(page, address));
        exact != addr_to_name_.end()) {
        return exact->second;
    }
    return lookup(address);
}

std::optional<SymbolAddress> SymbolTable::lookup_name_address(
    const std::string& name) const
{
    const auto found = name_to_addresses_.find(name);
    if (found == name_to_addresses_.end() || found->second.size() != 1)
        return std::nullopt;
    return found->second.front();
}

std::optional<uint16_t> SymbolTable::lookup_name(const std::string& name) const
{
    const auto found = lookup_name_address(name);
    return found ? std::optional<uint16_t>(found->address) : std::nullopt;
}

std::optional<SymbolAddress> SymbolTable::resolve_address(
    std::string_view text) const
{
    std::string value = trim(text);
    if (value.empty()) return std::nullopt;

    std::optional<uint8_t> requested_page;
    if (const size_t at = value.find_last_of('@'); at != std::string::npos) {
        const auto page = hex_value(value.substr(at + 1),
                                    std::numeric_limits<uint8_t>::max());
        if (!page) return std::nullopt;
        requested_page = static_cast<uint8_t>(*page);
        value = trim(std::string_view(value).substr(0, at));
        if (value.empty()) return std::nullopt;
    }

    if (const auto named = name_to_addresses_.find(value);
        named != name_to_addresses_.end()) {
        if (requested_page) {
            std::optional<SymbolAddress> exact_match;
            std::optional<SymbolAddress> logical_match;
            for (const auto& candidate : named->second) {
                if (candidate.page == requested_page) {
                    if (exact_match && !(*exact_match == candidate))
                        return std::nullopt;
                    exact_match = candidate;
                } else if (!candidate.page) {
                    if (logical_match && !(*logical_match == candidate))
                        return std::nullopt;
                    logical_match = candidate;
                }
            }
            if (exact_match) return exact_match;
            if (logical_match)
                return SymbolAddress{requested_page, logical_match->address};
        } else {
            std::optional<SymbolAddress> page_match;
            std::optional<SymbolAddress> logical_match;
            for (const auto& candidate : named->second) {
                auto& match = candidate.page ? page_match : logical_match;
                if (match && !(*match == candidate)) return std::nullopt;
                match = candidate;
            }
            if (page_match) {
                if (logical_match
                    && logical_match->address != page_match->address) {
                    return std::nullopt;
                }
                return page_match;
            }
            if (logical_match) return logical_match;
        }
    }

    const auto address = hex_value(value,
                                   std::numeric_limits<uint16_t>::max());
    if (!address) return std::nullopt;
    return SymbolAddress{requested_page, static_cast<uint16_t>(*address)};
}

std::optional<uint16_t> SymbolTable::resolve(std::string_view text) const
{
    const auto resolved = resolve_address(text);
    return resolved ? std::optional<uint16_t>(resolved->address) : std::nullopt;
}

void SymbolTable::clear()
{
    addr_to_name_.clear();
    logical_addr_to_name_.clear();
    name_to_addresses_.clear();
    loaded_file_.clear();
}
