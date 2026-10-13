#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Récepteur de logs pour rhythm-ps5.

Se connecte au serveur de log de l'appli (port 9025 par défaut), affiche tout
dans le terminal et l'enregistre dans logs/AAAA-MM-JJ_HH-MM-SS.log. Si l'appli
n'est pas encore lancée, on réessaie en boucle : lance ce script AVANT l'appli
pour ne rien rater du démarrage. Python 3 standard, aucune dépendance.

    python3 tools/logrecv.py 192.168.1.42
"""
import argparse
import datetime
import os
import socket
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("host", help="adresse IP de la PS5 (ou 127.0.0.1 pour la version PC)")
    parser.add_argument("--port", type=int, default=9025)
    parser.add_argument("--dir", default="logs", help="dossier des fichiers de log")
    parser.add_argument("--once", action="store_true", help="s'arrêter à la première déconnexion")
    args = parser.parse_args()

    os.makedirs(args.dir, exist_ok=True)
    waiting_shown = False
    while True:
        try:
            sock = socket.create_connection((args.host, args.port), timeout=1.0)
        except OSError:
            if not waiting_shown:
                print(f"... en attente de l'appli sur {args.host}:{args.port} (Ctrl+C pour quitter)", flush=True)
                waiting_shown = True
            time.sleep(0.2)
            continue
        waiting_shown = False
        name = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S") + ".log"
        path = os.path.join(args.dir, name)
        print(f"=== connecté, enregistrement dans {path}", flush=True)
        sock.settimeout(None)
        with open(path, "wb") as out:
            while True:
                try:
                    data = sock.recv(65536)
                except OSError:
                    data = b""
                if not data:
                    break
                out.write(data)
                out.flush()
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
        sock.close()
        print(f"=== connexion fermée (log : {path})", flush=True)
        if args.once:
            return


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
