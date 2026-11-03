# Reference inputs of reader_test.cpp

Made with picotool 2.3.1 (another writer than chip/picobin.hpp), so the reader is checked against blocks it did not
write itself:

| file | made with | what it is |
|---|---|---|
| `pt_ab.bin` | `picotool partition create pt_ab.json pt_ab.bin -t bin` | picotool's default A/B table: A sectors 2..512, B 513..1023 linked to A, ids 0/1, names, HASH_DEF + HASH_VALUE |
| `pt_ab_v2.bin` | `pt_ab_v2.json` (`pt_ab.json` with `"version": [2, 0]`) the same way | the same table, version 2.0: the slot 1 tests |
| `pt_ab_singleton_nohash.bin` | the same with `--singleton --no-hash` | the singleton flag, no hash items |
| `pt_named.bin` | `picotool partition create pt_named.json pt_named.bin -t bin` | three partitions with ids, names and an extra family id |
| `tiny_sealed.bin` | `picotool seal --hash --major 3 --minor 7 tiny.bin tiny_sealed.bin` | a 1 KiB image (vector table, the minimum Arm IMAGE_DEF at 0x100) sealed: picotool adds a second IMAGE_DEF at 0x400 (VERSION, LOAD_MAP, HASH_DEF, HASH_VALUE) and links the two |

`pt_ab.json` is picotool's `json/default-pt.json`, `pt_named.json` pico-examples' `flash/partition_info/pt.json`.
| `blink_sealed.bin` | `picotool seal --hash <a small Kvasir release ELF (an LED blink)> x.elf`, then `arm-none-eabi-objcopy -O binary` | a real image sealed: picotool's absolute load map (two flash entries) and SHA-256 |
| `copy_to_ram_sealed.bin` | pico-examples `hello_serial` for an RP2350 board with `-DPICO_COPY_TO_RAM=1` (pico-sdk, gcc), `picotool seal --hash --clear`, then `arm-none-eabi-objcopy -O binary` | load map entries a Kvasir image never has: a zero fill of all SRAM (`--clear`), a copy from flash into SRAM, two flash entries |
