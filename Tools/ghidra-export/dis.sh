#!/bin/bash
# dis.sh <tag> <start-va> <end-va>   -- raw disassembly of a VA range.
# Tags map to the vendor binaries; nothing is executed, only read by objdump.
set -eu
case "$1" in
  fw110) F="Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe";;
  fw107) F="old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe";;
  fw106) F="old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe";;
  fw104) F="old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe";;
  cfg107) F="Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe";;
  cfg104) F="old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.04.exe";;
  cfg101) F="old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.01.exe";;
  cfg100) F="old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.00.exe";;
  xm1r) F="XM1r_Flash_Upgrade_1.9.46.exe";;   # different product AND different vendor code base
  *) echo "unknown tag $1" >&2; exit 2;;
esac
cd "$(dirname "$0")/../.."
objdump -d --start-address="$2" --stop-address="$3" "$F"
