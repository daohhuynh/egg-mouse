# Vendor binaries

The Endgame Gear tools are Endgame Gear's proprietary software and are **not
redistributed here**. They are excluded by `.gitignore`.

To reproduce the analysis, download them from Endgame Gear and verify against
the hashes below. The filenames are the vendor's own, including their
inconsistent use of a `v` prefix.

Every binary listed has been present on the analysis machine. Nothing here is a
protocol claim; these are file identities only.

## Configuration Tool

| Version | Size (bytes) | SHA-256 |
| --- | --- | --- |
| 1.00 | 2193920 | `00ee4b2067b004e5bf94d240dd8ec13b461fdd2213f17e2f3278ab92355be9fc` |
| 1.01 | 1837568 | `1f2a20a4577cb22698136bb2096c8dd931001bb458d8a87c8f0583ea644fc455` |
| 1.04 | 1833472 | `38330b5c19dd424be68d7b15a68401bfd87db6d152c71c97698d67c7641a4e24` |
| 1.07 | 1836032 | `e0494dd635e0029fcac1a90e6fa09b00f3ab72d3cfd2abf33e33425153c36c4f` |

Exact filenames:

```
Endgame Gear OP1 8k v2 Configuration Tool v1.00.exe
Endgame Gear OP1 8k v2 Configuration Tool v1.01.exe
Endgame Gear OP1 8k v2 Configuration Tool v1.04.exe
Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe
```

## Firmware Updater

| Version | Size (bytes) | SHA-256 |
| --- | --- | --- |
| 1.04 | 2427904 | `0fd2016ff89e0ee4bce0d7eedc9b90ca696189090a15ce09ace4570fe8380f9f` |
| 1.06 | 2066944 | `221c1c49b6ee55a91a5fb12c0690031654035018875207d5f4886649af40a05d` |
| 1.07 | 2066944 | `d355346d2abefff094188cef927588c1fc7fa4f209a151f6684d57d189ba98e0` |
| 1.10 | 2133504 | `97cd23d8294461b412bcd02f4a2cadc23fb255dbea874d641d324729aa814144` |

Exact filenames:

```
Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe
Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe
Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe
Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe
```

1.06 and 1.07 are the same size but are different files.

## Local layout

The binaries live outside version control at the repository root:

```
Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe   # current
Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe      # current
old-config-executables/                               # 1.00, 1.01, 1.04
old-firmware-executables/                             # v1.04, v1.06, 1.07
```

Per CLAUDE.md 1.5, these are never executed. Static analysis only.
