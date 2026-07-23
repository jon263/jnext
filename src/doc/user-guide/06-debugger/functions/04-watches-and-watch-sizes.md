# Watches and watch sizes

A watch shows the live contents of an address in one of three sizes:

| Size | Bytes | Displayed as |
|---|---|---|
| Byte | 1 | `$XX` |
| Word | 2 | `$XXXX` |
| Long | 4 | `$XXXXXXXX` |

Word and Long are assembled little-endian, low byte first, matching the Z80's
own convention — so a Word watch on a `LD (nn),HL` destination reads back `HL`.

An unqualified watch reads through the CPU's current view of memory, so a watch
on a banked address follows whatever is paged in at that moment.

The address field accepts loaded symbol names as well as hexadecimal. Append an
8K page with `@`, for example `$C000@2A`, to pin the watch to that physical RAM
page. Labels imported from SLD `L` records carry their page automatically.
Pinned watches inspect inactive banks without changing the program's live MMU
mapping. A Word or Long that would cross the end of its selected 8K page shows
`--` instead of wrapping.

A watch added from the disassembly context menu is pinned to the RAM page that
was mapped when the menu opened. This keeps it attached to the selected code or
operand if the program later changes banks. Enter an address without `@` in the
Watch panel when you instead want the value to follow the live CPU mapping.
Invalid or ambiguous expressions show a warning and do not add a row.
