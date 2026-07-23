# Page-aware debugger symbols and watches

## Use case

ZX Spectrum Next programs reuse one 16-bit CPU address for many physical 8K
pages. A symbol or watch described only by `$C000` is therefore ambiguous once
code or data is banked. Cobra already uses this pattern for level data, audio,
sprites and streaming buffers, and future compiler-managed banking will use it
for executable code as well.

The debugger already keys SLD source traces, execute breakpoints and call frames
by physical page plus logical address. Symbols and watches are the remaining
user-visible pieces that lose the page.

## Inputs and compatibility

The design is compiler-neutral:

- existing Z88DK MAP, simple MAP and NextBuild `Memory.txt` symbols remain
  unqualified logical symbols;
- standard sjasmplus SLD v1 `L` records may supply page-qualified symbols or
  page-less logical wildcards;
- numeric addresses and logical symbol names may use the debugger's displayed
  `address @page` form to select a physical page;
- page-pinned watches read an 8K page directly and do not change the guest MMU.

An exact page-qualified symbol wins over an unqualified symbol at the same
logical address. Unqualified symbols remain the fallback, preserving existing
projects and UI behaviour. When Memory.txt and SLD define the same name and
logical address, the single SLD page refines that logical definition instead of
making normal name entry ambiguous. Different addresses or multiple SLD pages
remain ambiguous until the user supplies `@page`.

## Symbol model

A symbol address is `(optional page, uint16 address)`. The symbol table keeps
both directions:

- exact page/address to display name, with unqualified fallback;
- name to page/address, rejecting ambiguous duplicate names.

Successful SLD loading is transactional. Page-qualified labels are merged only
after the SLD structure and optional program identity have been accepted. A bad
or rejected SLD must not alter either the active source map or symbol table.

## Pinned watch model

A watch contains a logical address plus an optional physical page. Without a
page it reads through the live CPU MMU, exactly as before. With a page it reads
the page at `address & $1FFF` through a debugger-only, non-mutating MMU accessor.
The accessor observes the same Next page backing as a real mapping, including
the dedicated bank-5 and bank-7 memories.

The table displays pinned addresses as `$C000 @2A`. Byte, word and long watches
advance within the selected page and show `--` rather than wrapping if the value
would cross the 8K boundary. A watch created from the disassembly context menu
is pinned to the currently mapped RAM page so it continues to describe the
selected instruction or operand after a bank switch. Invalid or ambiguous
expressions produce a warning and do not add a watch.

## Out of scope

- Parsing Boriel `.obj` files or an unpublished linker format.
- Recognising or collapsing compiler far-call trampolines.
- Page-pinned data breakpoints; those require the memory-access callback to
  carry a physical page and deserve a separate change.
- Z88DK C line-table generation. Z88DK MAP symbols continue to work, but a
  future adapter must provide source traces.

## Verification

New tests must demonstrate that two different pages can own different labels at
the same logical address, that unqualified fallback remains compatible, that
malformed SLD input is transactional, and that pinned reads distinguish pages
without changing the active MMU mapping. A manual Cobra trial should pin watches
to at least one inactive asset/audio page while gameplay continues.
