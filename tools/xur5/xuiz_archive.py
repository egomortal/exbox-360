"""Read recovered XUIZ archives without converting their member payloads.

The offset base is the end of the COMPLETE name table, not each entry's end.
Version 1 counts UTF-16BE code units; other observed versions use byte names.
Duplicate names and original order are preserved. Callers choose name aliases
or last-entry-wins lookup explicitly; neither is an archive-level translation.
No files are written here, including names such as ../handles used by scenes.
"""
from __future__ import annotations
import struct
from collections.abc import Iterator


def members(data: bytes, *, normalize_slashes: bool = False) -> Iterator[tuple[str, bytes]]:
    """Yield validated names and byte-exact payloads in original archive order.

    Validate the entire table and every payload before yielding, so a truncated
    archive cannot be consumed as a misleading valid prefix. Bounds errors are
    ValueError rather than asserts, which must remain enabled under python -O.
    """
    if len(data) < 22 or data[:4] != b"XUIZ":
        raise ValueError("Not a complete XUIZ header")
    version = struct.unpack_from(">I", data, 4)[0]
    count = struct.unpack_from(">H", data, 20)[0]
    at, entries = 22, []
    for _ in range(count):
        if at + 9 > len(data):
            raise ValueError("Truncated XUIZ entry")
        size, offset, units = struct.unpack_from(">IIB", data, at)
        at += 9
        length = units * (2 if version == 1 else 1)
        if at + length > len(data):
            raise ValueError("Truncated XUIZ name")
        name = data[at:at + length].decode("utf-16be" if version == 1 else "latin1")
        at += length
        if normalize_slashes:
            name = name.replace("\\", "/")
        entries.append((name, offset, size))
    for name, offset, size in entries:
        if at + offset + size > len(data):
            raise ValueError(f"Truncated XUIZ member: {name}")
    for name, offset, size in entries:
        yield name, data[at + offset:at + offset + size]
