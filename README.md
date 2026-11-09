# tome_xad - XAD Client for Apple installer "tome" archives

An [XAD](https://dstoecker.eu/xadmaster.html) client library for extracting
**tome** archives created by Apple's installer (`softwareupdate` /
`Installer.app`) — the Apple Silicon / macOS update containers introduced with
macOS 13+.

tome is the Apple installer archive format: a container of versioned "sections"
describing installable payloads (ROM images, installers, drivers, …) together
with the compressed forks that make up the actual payloads. The payload forks
are compressed with the **InstaCompOne** decompression codec used by the Apple
installer SDK.

This client integrates tome archive support into `xadmaster.library`,
allowing tome archives to be listed and extracted from any XAD-compatible tool
(e.g. `xadExtract`, `UnArc`, or directly via the API).

## Tome Archive Format

| Field        | Offset | Size  | Description                                    |
|--------------|--------|-------|------------------------------------------------|
| Magic        | +0     | 4     | `0x6B630001` (`kc 00 01`)                     |
| Reserved     | +4     | 24    |                                                 |
| RecordCount  | +28    | 4     | Number of section records (BE)                |
| Record       | +36    | 128   | One per record, count entries                 |

Each section record (128 bytes):

| Field          | Offset | Size  | Description                                    |
|----------------|--------|-------|------------------------------------------------|
| ID             | +4     | 2     | Record ID (BE)                                 |
| NameLen        | +6     | 1     | Length of name                                 |
| Name           | +7     | 31    | Name (Str31, left-padded with zeros)          |
| Type           | +38    | 4     | File type (e.g. `"tbxi"` for ROM images)      |
| Creator        | +42    | 4     | File creator (e.g. `"chrp"`)                  |
| Version       | +54    | 2     | Version (BE)                                   |
| Data Size     | +60    | 4     | Uncompressed data size                        |
| Data Offset   | +64    | 4     | Offset of compressed data fork                |
| Data CSize    | +68    | 4     | Compressed data fork size                     |
| Res Size      | +76    | 4     | Uncompressed resource fork size               |
| Res Offset    | +80    | 4     | Offset of compressed resource fork           |
| Res CSize     | +84    | 4     | Compressed resource fork size                 |

Forks use a 4-byte block header followed by the payload:

| Field        | Value     | Description                                    |
|--------------|-----------|------------------------------------------------|
| Stored       | `01 94 FF FF` | Uncompressed fork block                 |
| CHRP         | `"<CHRP-BOOT>"` | Installer ROM fork with CHRP wrapper   |
| Codec        | `02 00`   | InstaCompOne compressed block (per 64 KiB block) |

## Files

| File           | Description                                    |
|---------------|------------------------------------------------|
| `tome_xad.c`    | XAD client source                              |
| `xadClient.h`  | XAD API type definitions and prototypes       |
| `Tome`| Build output: XAD client shared library   |
| `README.md`     | This file                                      |

## Building

The client is written in **ANSI C (C89)** and builds with any ANSI C compiler
(e.g. `cc`, `gcc` or `clang`):

```sh
make
```

The build produces `Tome` — a shared library that follows the XAD client
contract:

- Client identification is based on the `0x6B630001` (`kc 00 01`) tome magic
- The `Mac OS ROM` payload is unwrapped from the data fork and handed to the
  XAD installation machinery for the current file
- Supports both stored and InstaCompOne compressed forks

## Installation

```sh
make install
```

Installs the shared library to `LIBS:xad/`:

```sh
make install
```

## Usage

Once installed, `xadmaster.library` automatically loads the client and
recognizes tome archives. Any XAD-compatible tool will then be able to list and
extract tome archives.

The client exposes the programmatic entry points used by XAD compatible
tools:

```c
int xad_tome_client_install(const unsigned char *data, unsigned int size, const char *output_dir);
int xad_tome_client_recognize(const unsigned char *data, unsigned int size);
const char *xad_tome_client_name(void);
```

- `xad_tome_client_recognize` - checks whether an in-memory image is a tome
- `xad_tome_client_install` - installs the ROM payload into `output_dir`
- `xad_tome_client_name` - returns the client name

## Notes

- The InstaCompOne decoder implements the literal / copy (LZ77) Huffman
  coding used by Apple installer streams, including compact Huffman codes and
  backward distance decoding.
- The original Apple installer streams are encrypted; the reference
  implementations this client is ported from provide the decoding algorithm.

## License

This XAD client is provided as-is for use on AmigaOS systems and compatible
platforms.
