# Passagem de contexto — Clawdmeter (sessão de 12–13/09/2026)

Leia também o `CLAUDE.md` do projeto. Responder ao usuário em português.

## Hardware
- A placa do usuário é a **Guition/Sunton ESP32-4848S040**: tela ST7701 RGB de 4", 480×480, toque GT911
  (SDA 19 / SCL 45), luz de fundo no GPIO 38, conversor CH340 (`/dev/cu.usbserial-*`), **sem botões e
  sem bateria**. **Não é** a Waveshare AMOLED-2.16.
- Port criado em `firmware/src/boards/guition_4848s040/`, env `guition_4848s040` no `platformio.ini`.
- Gravar: `pio run -d firmware -e guition_4848s040 -t upload --upload-port $(ls /dev/cu.usbserial-* | head -1)`
  (upload só funciona a 115200; leva ~2 min).
- O firmware de relógio que veio de fábrica foi apagado e não tinha backup.

## Daemon (macOS)
- Instalado via `install-mac.sh` e rodando pelo launchd (`com.user.claude-usage-daemon`).
- Reiniciar depois de mudar o código: `launchctl kickstart -k gui/$(id -u)/com.user.claude-usage-daemon`
- Log: `~/Library/Logs/claude-usage-daemon.out.log`
- O Mac pareia com a placa em Ajustes → Bluetooth → Clawdmeter.

## O que foi implementado (branch `meu-clawdmeter`, **tudo sem commit**)
Telas, em ordem de toque: Clawd → Uso → Histórico → Modelos → Cripto → Bolsa → Vasco.
- **Português** em toda a interface. As fontes foram regeradas com Latin-1 (`0xA0-0xFF`) e o
  `docs/fonts.md` foi atualizado.
- **Histórico**: % da sessão de 5h nas últimas 24h, em bins de 15 min. O daemon grava as leituras em
  `~/.config/claude-usage-monitor/history.json` e o módulo fica em `daemon/usage_extras.py`.
- **Modelos**: tokens por modelo na janela de 5h atual, lidos de `~/.claude/projects/**/*.jsonl`. Só conta
  o Claude Code deste Mac.
- **Cripto**: 8 moedas com R$ e US$ via CoinGecko (lista `COINS` em `daemon/market_quotes.py`).
- **Bolsa**: 13 ações + Ibovespa via Yahoo Finance, em 2 páginas que viram sozinhas (lista `STOCKS` no
  mesmo arquivo). O Yahoo exige User-Agent de navegador.
- **Vasco**: próximos 4 jogos pela API da ESPN (time 3454), módulo `daemon/team_fixtures.py`. A ESPN
  bloqueia User-Agent de navegador.
- **Rotação automática** (`ROTATION` em `firmware/src/ui.cpp`, ciclo de 2 min): Uso 40%, Histórico 10%,
  Modelos 10%, Cripto 20%, Bolsa 10%, Vasco 10%. Tocar avança de tela e pausa a rotação por 60 s.
- Luz de fundo no máximo (escala no `display.cpp` da placa).

## Protocolo daemon → placa
- Cada escrita BLE é sem confirmação e tem limite de ~180 bytes. Por isso cada tipo de dado vai numa
  mensagem própria, com 0,4 s entre elas:
  `{"s":...}` uso · `{"h":"<96 chars base64>"}` histórico · `{"m":[[nome,tokens]]}` modelos ·
  `{"x"|"b"|"v": linhas, "o": offset, "n": total, "i": índice}`.
- `chunk_table()` em `market_quotes.py` divide as tabelas em mensagens de até 150 bytes.
- O firmware trata essas chaves em `handle_extra_json()` no `main.cpp`.

## Armadilhas já encontradas
- A memória do LVGL acabou uma vez (um label por célula travava o boot). As tabelas usam **um label por
  coluna**. `LV_MEM_SIZE=98304` só nos envs guition e sim; hoje fica em ~52% de uso.
- Testar interface no simulador antes de gravar:
  `pio run -d firmware -e sim`, depois
  `SDL_VIDEODRIVER=dummy SIM_SCENARIO=<arquivo.jsonl> SIM_AUTOSHOT_MS=5000 SIM_AUTOSHOT_PATH=x.bmp firmware/.pio/build/sim/program`
  (rodar de dentro de `firmware/`). Para capturar outra tela, trocar temporariamente
  `ui_show_screen(SCREEN_SPLASH)` no `main.cpp` e depois reverter.
- Testes do daemon: `pytest daemon/tests/test_usage_extras.py daemon/tests/test_market_quotes.py daemon/tests/test_team_fixtures.py daemon/tests/test_macos_multidir.py daemon/tests/test_freeride.py`
  (38 passando). O pytest não está no venv do daemon; instale num diretório temporário e rode com `PYTHONPATH`.
- Só o daemon de macOS foi atualizado; os de Linux e Windows não mandam os dados novos.

## Estado em 13/09/2026 (fim da sessão)
- Tudo commitado e publicado em https://github.com/diasgthiago92/Clawdmeter (branch `meu-clawdmeter`).
- As diretrizes do usuário estão no topo do `CLAUDE.md` (listas roláveis, todas as telas arrastáveis,
  cores Claude laranja / Kiro roxo, Kiro nunca com camisa do Flamengo).
- Telas, em ordem de toque: Clawd → Uso → Agenda de Hoje → Consumo - 24 horas → Modelos →
  Rotinas Automáticas → Criptomoedas → Bovespa → Fundos Imobiliários → Próximos Jogos do Vasco (→ Vasco ao vivo durante jogos).
- Fontes novas do daemon: `google_agenda.py` (login único via `google_calendar_login.py`),
  `kiro_routines.py` (Slack #leo-dias-news), `kiro_usage.py` (créditos nos logs do Kiro IDE +
  atividade do kiro-cli), `team_fixtures.py` (ESPN, inclusive placar ao vivo).
- Vídeo de demonstração: `pio run -e sim_demo` + `SIM_RECORD=demo.mp4` com `firmware/sim/demo.jsonl`.
- O Bluetooth agora inicia antes da tela (evita boot loop na Guition); o LVGL tem pool extra de 256 KB na PSRAM.
