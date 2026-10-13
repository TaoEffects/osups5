#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Envoie le dossier d'appli sur la PS5 par FTP (serveur FTP déjà lancé sur la console).
#   bash ps5/deploy.sh 192.168.1.42 [port-ftp]
# Utilise la commande `make deploy` du boilerplate (envoi atomique, eboot.bin en dernier).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# -ge 1 ]] || { echo "usage : $0 <ip-ps5> [port-ftp, 2121 par defaut]" >&2; exit 2; }
[[ -f $root/build/ps5/app/Makefile ]] || bash "$root/ps5/build.sh"
make -C "$root/build/ps5/app" --no-print-directory deploy PS5_HOST="$1" FTP_PORT="${2:-2121}"
