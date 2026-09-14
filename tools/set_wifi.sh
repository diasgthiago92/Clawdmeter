#!/bin/bash
# Grava a rede Wi-Fi que a placa usa no modo Wi-Fi (sem o Mac).
# O daemon entrega essas credenciais à placa pelo Bluetooth pareado.
set -euo pipefail
CONF="$HOME/.config/claude-usage-monitor/wifi.json"
current=$(networksetup -getairportnetwork en0 2>/dev/null | sed -n 's/^Current Wi-Fi Network: //p')
read -r -p "Nome da rede (SSID)${current:+ [$current]}: " ssid
ssid=${ssid:-$current}
read -r -s -p "Senha (não aparece na tela): " pass
echo
mkdir -p "$(dirname "$CONF")"
umask 077
/usr/bin/python3 -c 'import json,sys; json.dump({"ssid": sys.argv[1], "password": sys.argv[2]}, open(sys.argv[3], "w"))' "$ssid" "$pass" "$CONF"
chmod 600 "$CONF"
echo "Pronto: $CONF (a placa recebe no próximo ciclo do daemon, em até 1 minuto)."
