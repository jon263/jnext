#pragma once

#include <string>

class SourceMap;
class SymbolTable;

struct SourceMapLoadResult {
    int count = -1;
    std::string error;
    int symbol_count = 0;

    explicit operator bool() const { return count >= 0; }
};

/// Load sjasmplus SLD v1 source traces into a compiler-neutral SourceMap.
/// If symbols is supplied, merge page-qualified SLD L records transactionally.
SourceMapLoadResult load_sld(SourceMap& map, const std::string& path,
                             SymbolTable* symbols = nullptr);

/// Load <program>.sld or <program>.sld.txt beside a program image.
SourceMapLoadResult load_sld_sidecar(SourceMap& map,
                                     const std::string& program_path,
                                     SymbolTable* symbols = nullptr);
