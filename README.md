# Meu Clawdmeter

Versão pessoal do [Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter) rodando numa
**Guition ESP32-4848S040** (tela touch de 4", 480×480). Mostra o consumo de **Claude, Kiro e Gemini**, a agenda,
rotinas automáticas, cotações, jogos do Vasco, e tem mascotes em pixel art que mudam de roupa conforme o dia.

> **Sobre as imagens:** as marcadas como **placa** são capturas reais da tela do aparelho (framebuffer lido
> pela serial, com dados reais). As marcadas como **simulador** rodam o mesmo firmware no simulador de desktop,
> com dados de exemplo, nas telas que mostrariam agenda, rotinas e commits pessoais.

## Telas

| Consumo Atual · placa | Consumo 24 horas · placa | Criptomoedas · placa |
| :---: | :---: | :---: |
| <img src="docs/meu-clawdmeter/placa-consumo-atual.png" width="260"> | <img src="docs/meu-clawdmeter/placa-consumo-24h.png" width="260"> | <img src="docs/meu-clawdmeter/placa-criptomoedas.png" width="260"> |
| Claude, Gemini e Kiro, cada um na sua cor | Barras por hora das três IAs | O Kiro espiando na borda direita |

| Bovespa · simulador | Fundos Imobiliários · simulador | Jogos do Vasco · placa |
| :---: | :---: | :---: |
| <img src="docs/meu-clawdmeter/sim-bovespa.png" width="260"> | <img src="docs/meu-clawdmeter/sim-fiis.png" width="260"> | <img src="docs/meu-clawdmeter/placa-jogos-do-vasco.png" width="260"> |
| 38 ações com P/VP, lista rolável | 30 FIIs | Próximos jogos (ESPN) |

| Agenda de Hoje · simulador | Rotinas Automáticas · simulador | Últimas Ações · simulador |
| :---: | :---: | :---: |
| <img src="docs/meu-clawdmeter/sim-agenda.png" width="260"> | <img src="docs/meu-clawdmeter/sim-routines.png" width="260"> | <img src="docs/meu-clawdmeter/sim-actions.png" width="260"> |
| Google Calendar, abre na reunião atual | Rotinas do dia, com "Rodar de novo" se falhar | Últimos commits de cada IA |

| Reunião chegando · simulador | Gol do Vasco · simulador |
| :---: | :---: |
| <img src="docs/meu-clawdmeter/sim-meeting.png" width="260"> | <img src="docs/meu-clawdmeter/sim-goal.png" width="260"> |
| Contagem regressiva e botão **Começar**, que abre o Meet no Mac | O Almirante comemora na tela do jogo ao vivo |

## Mascotes

No canto de todas as telas os mascotes se revezam: **Clawd** (Claude) e o fantasma do **Kiro**. Em datas
especiais eles usam fantasia: **camisa do Vasco** em dia de jogo, Papai Noel no Natal, passista no Carnaval e
bruxa no Halloween.

| No canto das telas (dia de jogo) · simulador | |
| :---: | :---: |
| <img src="docs/meu-clawdmeter/sim-corner_clawd-topo.png" width="380"> | <img src="docs/meu-clawdmeter/sim-corner_kiro-topo.png" width="380"> |
| Clawd com a camisa do Vasco | Kiro com a camisa do Vasco |
| <img src="docs/meu-clawdmeter/sim-corner_almirante-topo.png" width="380"> | <img src="docs/meu-clawdmeter/sim-corner_almirante_andando-topo.png" width="380"> |
| Almirante no lugar dele | Almirante saindo andando |

| Tela inicial · simulador | | |
| :---: | :---: | :---: |
| <img src="docs/meu-clawdmeter/sim-palco_clawd.png" width="200"> | <img src="docs/meu-clawdmeter/sim-palco_kiro.png" width="200"> | <img src="docs/meu-clawdmeter/sim-palco_almirante.png" width="200"> |
| Clawd | Kiro | Almirante |

### Almirante

O mascote do Vasco, convertido em pixel art a partir da ilustração original (`assets/almirante/`) por
`tools/make_almirante.py`. Aparece **só a partir de 2 horas antes de um jogo do Vasco**; a camisa do Vasco no
Clawd e no Kiro vale o dia inteiro.

| Animações | Passeio no canto · simulador | Tela inicial · simulador |
| :---: | :---: | :---: |
| <img src="docs/meu-clawdmeter/almirante-animado.gif" width="170"> | <img src="docs/meu-clawdmeter/almirante-passeio.gif" width="260"> | <img src="docs/meu-clawdmeter/palco-almirante.gif" width="260"> |
| Parado (soco no ar, piscada, respiração), caminhada e pulo de comemoração | Sai andando, aparece grande na borda comemorando e volta | Percorre o palco e pula ao voltar |

## O que foi feito para ficar mais elegante

- **Títulos limpos:** todos em Tiempos 34, numa linha, alinhados à esquerda logo depois do espaço onde o mascote
  se mexe. Antes, títulos grandes e centralizados quebravam em duas ou três linhas e encostavam em outros elementos.
- **Nomes mais claros:** "Consumo" virou **Consumo Atual**, "Próximos Jogos do Vasco" virou **Jogos do Vasco** e a
  antiga tela "Modelos" deu lugar a **Últimas Ações**.
- **Bateria do mouse e do teclado com ícones:** pequenos ícones Lucide com o percentual, numa linha fina no topo
  direito, sem disputar espaço com o título. Abaixo de 20% ficam vermelhos. A leitura vem direto do Bluetooth.
- **Uma cor por IA, em todo lugar:** Claude laranja (`#d97757`), Kiro roxo (`#9046ff`) e Gemini azul-claro
  (`#64b5f6`). Números e barras na cor da ferramenta, textos em branco.
- **Listas roláveis, nunca páginas:** tabelas longas deslizam sozinhas devagar e rolam com o dedo; cabeçalho e
  rodapé ficam fixos.
- **Linhas de uma só altura:** textos longos terminam em "..." em vez de quebrar e empurrar a lista.
- **Menos poluição:** nomes de reunião, rotinas e commits cabem numa linha; horários na cor da IA à esquerda.
- **Mascotes com personalidade:** fantasias por data, o Kiro que espia grande pela borda, e o Almirante com
  caminhada, comemoração e reação a gol.
- **Botões só quando fazem sentido:** o **Começar** da reunião só aparece quando há link de vídeo, e o
  **Rodar de novo** só quando a rotina falhou.

## Onde está cada coisa

| Parte | Arquivo |
| --- | --- |
| Telas e layout | `firmware/src/ui.cpp` |
| Mascotes e animações | `firmware/src/splash.cpp`, `tools/make_kiro_ghost.py`, `tools/make_almirante.py` |
| Daemon do Mac (envia os dados por Bluetooth) | `daemon/claude_usage_daemon.py` |
| Últimos commits das IAs | `daemon/ai_actions.py` |
| Bateria do mouse e do teclado | `daemon/peripheral_battery.py` |
| Agenda e botão Começar | `daemon/google_agenda.py` |
| Jogos e janela do Almirante | `daemon/team_fixtures.py` |

---

# Projeto original (em inglês)

# Clawdmeter

<img src="assets/readme/waving.gif" width="120" align="right" alt="">

A small ESP32 dashboard I made for my desk to keep an eye on Claude Code usage.

It runs on a [Waveshare ESP32-S3-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm?&aff_id=149786) as well as a few other alternative boards and pairs over Bluetooth, the splash screen plays pixel-art Clawd animations that get
busier when your usage rate climbs. The two side buttons send Space and
Shift+Tab over BLE HID for Claude Code's voice mode and mode-toggle shortcuts.

<img width="1179" height="994" alt="Usage meter" src="https://github.com/user-attachments/assets/83e54aea-0932-428f-94aa-b3ede3a360aa" />

## Screens

The device boots into the splash. Tap the screen anywhere to switch to the Usage view; tap again to flip back to the splash.

|              Splash               |              Usage              |
| :-------------------------------: | :-----------------------------: |
| ![Splash](screenshots/splash.gif) | ![Usage](screenshots/usage.png) |
|   Splash; touch-toggle anytime    | Session and weekly utilization  |

While the splash is up, the middle (PWR) button cycles animations. **Hold the power button for 3 seconds, then release, to put the device into pairing mode** — this clears the saved Bluetooth bond and re-advertises. The firmware also auto-rotates animations every 20 s within the current usage-rate group, so a long stretch on the splash isn't just one Clawd on loop.

## Hardware

Boards supported out of the box:

- [Waveshare ESP32-S3-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm?&aff_id=149786)
- [Waveshare ESP32-C6-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-c6-touch-amoled-2.16.htm?&aff_id=149786)
- [Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm?&aff_id=149786)
- [Waveshare ESP32-C6-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm?&aff_id=149786)
- [Waveshare ESP32-S3-Touch-AMOLED-2.06](https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm?&aff_id=149786)
- [Waveshare ESP32-S3-Touch-LCD-1.54](https://www.waveshare.com/esp32-s3-lcd-1.54.htm?sku=33869&aff_id=149786)
- [Waveshare ESP32-S3-Touch-LCD-4](https://www.waveshare.com/esp32-s3-touch-lcd-4.htm)

> Please check if a pull request exists for your alternative hardware port before opening a new one, providing QA feedback and testing on the same hardware is more valuable than duplicate pull requests.

**Porting to another board:** the firmware is a thin HAL with per-board folders under `firmware/src/boards/`. Drop in a new folder and a new PlatformIO env — `main.cpp`, `ui.cpp`, and `splash.cpp` never need to change. See [`docs/porting/adding-a-board.md`](docs/porting/adding-a-board.md) for the walk-through and [`docs/porting/hal-contract.md`](docs/porting/hal-contract.md) for the interfaces a port must implement.

## Prerequisites

- Linux (tested on Ubuntu), macOS, or Windows 10/11
- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html)
- Linux: `curl`, `bluetoothctl`, `busctl` (BlueZ Bluetooth stack)
- macOS: `python3` (the installer sets up a venv with `bleak` and `httpx`)
- Windows: `python3` 3.11+ (the installer sets up a venv with `bleak`, `httpx`, and `pystray`)
- Claude Code with an active subscription

## macOS installation

The macOS host pieces — Python daemon, LaunchAgent, and flash helper — were ported by [Chris Davidson (@lorddavidson)](https://github.com/lorddavidson). Thanks Chris!

### Flash the firmware

```bash
./flash-mac.sh waveshare_amoled_216                       # ESP32-S3 2.16" (auto-detects /dev/cu.usbmodem*)
./flash-mac.sh waveshare_amoled_216_c6                    # ESP32-C6 2.16" variant
./flash-mac.sh waveshare_amoled_18  /dev/cu.usbmodem1101  # ESP32-S3 1.8" (or pass an explicit USB serial port)
```

The board env name is required. Run `./flash-mac.sh` with no args to see the available envs (scraped from `firmware/platformio.ini`).

### Pair the device

After flashing, open **System Settings → Bluetooth** and click _Connect_ next to "Clawdmeter". The daemon only ever connects to the peripheral this Mac is paired/connected to — it never scans for a nearby device — so once it's connected here the daemon picks it up on its next poll (~60 s).

### Install the daemon

The daemon reads your Claude OAuth token from the macOS Keychain (service `Claude Code-credentials`), polls usage every 60 s, and pushes it to the display over BLE.

```bash
./install-mac.sh
```

The installer creates a Python venv in `daemon/.venv/`, installs `bleak` and `httpx`, renders a LaunchAgent into `~/Library/LaunchAgents/com.user.claude-usage-daemon.plist`, and loads it. The first run is launched interactively so macOS prompts for Bluetooth permission.

Useful commands:

```bash
launchctl list | grep claude-usage                                          # check it's running
tail -F ~/Library/Logs/claude-usage-daemon.out.log                          # live logs
launchctl unload ~/Library/LaunchAgents/com.user.claude-usage-daemon.plist  # stop
launchctl load -w ~/Library/LaunchAgents/com.user.claude-usage-daemon.plist # start
```

## Linux installation

### Flash the firmware

```bash
./flash.sh waveshare_amoled_216                  # ESP32-S3 2.16" (defaults to /dev/ttyACM0)
./flash.sh waveshare_amoled_216_c6               # ESP32-C6 2.16" variant
./flash.sh waveshare_amoled_18  /dev/ttyACM1     # ESP32-S3 1.8" (or pass an explicit USB serial port)
```

The board env name is required. Run `./flash.sh` with no args to see the available envs (scraped from `firmware/platformio.ini`).

### Pair the device

After flashing, the device advertises as "Clawdmeter". Pair it once:

```bash
# Scan for the device
bluetoothctl scan le

# When "Clawdmeter" appears, pair and trust it
bluetoothctl pair F4:12:FA:C0:8F:E5    # use your device's MAC
bluetoothctl trust F4:12:FA:C0:8F:E5
```

To re-pair later, hold the power button for 3 seconds then release — the device clears its saved bond and re-advertises.

### Install the daemon

The daemon polls your Claude usage every 60 seconds and sends it to the display over BLE.

```bash
./install.sh
systemctl --user start claude-usage-daemon
```

Check status: `systemctl --user status claude-usage-daemon`

View logs: `journalctl --user -u claude-usage-daemon -f`

## Windows installation

Runs natively on Windows — no WSL required. A system-tray app polls your usage and pushes it over BLE, and starts automatically at login.

### Prerequisites

- **Native Windows** (not WSL).
- **Python 3.11+** from [python.org](https://www.python.org/downloads/) — check _"Add python.exe to PATH"_ during install.
- **Claude Code** installed, with `claude login` completed. The token is read from `%USERPROFILE%\.claude\.credentials.json` (falling back to `%LOCALAPPDATA%\Claude\` then `%APPDATA%\Claude\`).
- The repo on a **native Windows path** (e.g. `%USERPROFILE%\Clawdmeter`), **not** a `\\wsl$` share — the installer refuses a WSL path.

### Flash the firmware

```powershell
pio run -d firmware -e waveshare_amoled_216 -t upload --upload-port COM5   # use your device's COM port
```

Run `pio run -d firmware` with no env to see the available board envs.

### Pair the device

The device is a bonded BLE HID keyboard, so pair it once: **Settings → Bluetooth & devices → Add device → Bluetooth**, then select "Clawdmeter". Pairing is **required** — it enables the physical buttons and keeps a persistent connection (the device keeps showing your last-synced usage even after the daemon quits). To undo, use **Remove device** (this disables the buttons).

### Install the daemon (recommended)

From the repo root in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File install-windows.ps1
```

This creates a venv, installs `bleak`/`httpx`/`pystray`/`Pillow` from the in-repo requirements (no internet downloads), registers a per-user login-autostart entry (`HKCU\…\Run`, no admin needed), and launches the tray app headlessly (no console window).

### Run manually instead (optional)

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1        # if blocked: Set-ExecutionPolicy -Scope CurrentUser RemoteSigned, then retry
pip install -r daemon\requirements-windows.txt
python daemon\claude_usage_daemon_windows.py        # runs in the foreground; Ctrl+C to stop
```

### Tray icon and menu

The icon's corner bubble shows state — **green** Connected, **amber** Scanning, **red** Error — and hovering shows the status (`Connected · last update HH:MM`). A notification fires once when it enters Error (e.g. an expired token). Right-click for the menu:

- **Status header** — live state + last sync time.
- **Start at login** — toggle autostart on/off.
- **Quit** — stops the daemon cleanly; leaves the Windows pairing intact (device keeps its last reading).

### Logs and troubleshooting

```powershell
Get-Content $env:LOCALAPPDATA\Clawdmeter\daemon.log -Tail 30        # view logs
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v Clawdmeter /f   # remove autostart
```

| Symptom                                | Fix                                                      |
| -------------------------------------- | -------------------------------------------------------- |
| `Device not found`                     | Power on the device; make sure it's in range and paired. |
| `token expired` toast / `API HTTP 401` | Re-run `claude login`, then restart the daemon.          |
| `Connection failed`                    | Toggle Windows Bluetooth off/on in Settings.             |
| `Warning: running under Linux/WSL`     | Run from a native PowerShell window, not a WSL shell.    |

## How it works

<img src="assets/readme/magnifier.gif" width="150" align="right" alt="">

1. The daemon reads your Claude Code OAuth token — from the macOS Keychain (service `Claude Code-credentials`) on macOS, or from `~/.claude/.credentials.json` on Linux (`%USERPROFILE%\.claude\.credentials.json` on Windows).
2. It makes a minimal API call to `api.anthropic.com/v1/messages` — one token of Haiku, basically free.
3. The usage numbers come straight out of the response headers (`anthropic-ratelimit-unified-5h-utilization` and friends).
4. The daemon connects to the ESP32 over BLE and writes a JSON payload to the GATT RX characteristic.
5. The firmware parses it and updates the LVGL dashboard.
6. The firmware also tracks the rate of change of session % over a 5-minute window and picks splash animations from the matching mood group.
7. The two side buttons are independent of all of this — they send Space and Shift+Tab as BLE HID keyboard input to the paired host directly.

## Physical buttons

The board has three side buttons. Left and right send HID keys; the middle (PWR) button cycles splash animations and, held for 3 seconds, triggers pairing mode.

| Button           | GPIO         | Function                                                     |
| ---------------- | ------------ | ------------------------------------------------------------ |
| **Left**         | GPIO 0       | Hold to send Space (Claude Code voice-mode push-to-talk)     |
| **Middle** (PWR) | AXP2101 PKEY | On splash: cycle animations. Hold 3s + release: pairing mode |
| **Right**        | GPIO 18      | Press to send Shift+Tab (Claude Code mode toggle)            |

Space and Shift+Tab go out as standard BLE HID keyboard reports, so they trigger in whatever window has focus on the paired host — not just Claude Code.

## BLE protocol

The device advertises a custom GATT service alongside the standard HID keyboard service:

|                            | UUID                                   |
| -------------------------- | -------------------------------------- |
| **Data Service**           | `4c41555a-4465-7669-6365-000000000001` |
| RX Characteristic (write)  | `4c41555a-4465-7669-6365-000000000002` |
| TX Characteristic (notify) | `4c41555a-4465-7669-6365-000000000003` |
| **HID Service**            | `00001812-0000-1000-8000-00805f9b34fb` |

JSON payload format (written to RX):

```json
{ "s": 45, "sr": 120, "w": 28, "wr": 7200, "st": "allowed", "ok": true }
```

Fields: `s` = session %, `sr` = session reset (minutes), `w` = weekly %, `wr` = weekly reset (minutes), `st` = status, `ok` = success flag.

## Development

<img src="assets/readme/crab.gif" width="120" align="right" alt="">

- **Desktop simulator** — iterate on the UI without hardware: an SDL2 window
  runs the full firmware loop with scenario playback (`pio run -d firmware -e
sim`, then `cd firmware && .pio/build/sim/program`). See
  [`SIM-USAGE.md`](SIM-USAGE.md) for controls, scenarios, and headless
  screenshots.
- **Splash animations** — Anthropic's official Clawd sprites, archived with
  provenance notes in [`research/clawd-official/`](research/clawd-official/);
  `node tools/convert_official_clawd.js` regenerates
  `firmware/src/splash_animations.h`. See [`tools/README.md`](tools/README.md).
- **Icons** — Lucide PNGs convert to LVGL C arrays with
  `tools/png_to_lvgl.js`. See [`tools/README.md`](tools/README.md).
- **Fonts** — the pre-compiled LVGL fonts and the LVGL-9 patching they need:
  [`docs/fonts.md`](docs/fonts.md).
- **Porting** — [`docs/porting/adding-a-board.md`](docs/porting/adding-a-board.md)
  and [`docs/porting/hal-contract.md`](docs/porting/hal-contract.md).

## Credits

- Pixel-art Clawd animations are Anthropic's official mascot art (claude.ai/code, Claude Code desktop), archived and converted by the tooling in `tools/` and `research/clawd-official/`.
- Lucide icon set ([lucide.dev](https://lucide.dev), MIT) for bluetooth and battery UI glyphs.
- Anthropic brand fonts (Tiempos Text, Styrene B) — see licensing warning below.

## Licensing gray area warning

The software in this repository uses and adheres to the Anthropic brand guidelines and uses the same proprietary fonts that Anthropic has a license for but this software uses without permission as well as using assets from Anthropic such as the copyrighted Clawd mascot so even though the code in this repo is non-proprietary I will not license it myself under a copyleft license since this repo includes proprietary fonts and copyrighted assets. Please be aware of this if you fork or copy the code from this repo. **You have been warned!**
