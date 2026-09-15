# Passagem de contexto — Clawdmeter (atualizado em 14/09/2026)

Leia primeiro as **Diretrizes do projeto** no topo do `CLAUDE.md` (valem sempre). Responder em português.

## Hardware e gravação
- Placa: **Guition/Sunton ESP32-4848S040** (ST7701 RGB 480×480, toque GT911, CH340 `/dev/cu.usbserial-*`,
  sem botões/bateria). Env `guition_4848s040`. **Não é** a Waveshare AMOLED-2.16.
- Gravar (~2–5 min, só 115200):
  `pio run -d firmware -e guition_4848s040 -t upload --upload-port $(ls /dev/cu.usbserial-* | head -1)`
- Serial: abrir a porta **reinicia a placa**. BLE sobe antes da tela (evita boot loop por falta de memória).
- LVGL: pool interno 96 KB + 256 KB na PSRAM (`LV_MEM_POOL_EXPAND_SIZE`). Sem memória, `lv_obj_create`
  trava num assert em laço infinito.

## Daemon (macOS)
- launchd `com.user.claude-usage-daemon`. Reiniciar após mudar código:
  `launchctl kickstart -k gui/$(id -u)/com.user.claude-usage-daemon` · log `~/Library/Logs/claude-usage-daemon.out.log`
- Testes: `pytest` não está no venv; instale num dir temporário e rode com `PYTHONPATH` os arquivos
  `daemon/tests/test_*.py` (menos os de Windows/Linux).

## Telas (ordem de toque: direita avança, esquerda volta)
Clawd (tela inicial) → **Consumo Atual** (painéis Claude Daily, Gemini Daily, Kiro Monthly, Claude Weekly) →
**Agenda de Hoje** → **Consumo - 24 horas** (barras Claude/Gemini/Kiro) → **Últimas Ações** (últimas ações de Claude, Kiro e Gemini) → **Rotinas Automáticas** →
**Criptomoedas** → **Bovespa** (38 ações, P/VP) → **Fundos Imobiliários** (30 FIIs) → **Jogos do Vasco**.
Telas de alerta que travam a rotação: **Reunião** (5 min antes, contagem; botão "Começar" abre o link Meet/Zoom/Teams no Mac via `{"mg":1}`), **Rotina falhou** (vermelha, botão
"Rodar de novo"), **Vasco ao vivo**.

## Fontes de dados (daemon/)
- Claude: API de uso + transcripts (`usage_extras.py`). Gemini = Antigravity CLI (`antigravity_usage.py`, bancos
  em `~/.gemini/antigravity-cli/conversations`, formato reverso-engenheirado).
- Kiro: créditos do mês nos logs do Kiro IDE (real); 24h/5h = requisições do kiro-cli × crédito médio (estimado)
  (`kiro_usage.py`). Rotinas: Slack #leo-dias-news (`kiro_routines.py`, rerun só via lista fixa + launchctl).
- Últimas Ações (`ai_actions.py`, payload `{"a": [[ia, "HH:MM" ou "dd/mm", assunto]], "o", "n"}`): últimos commits de cada IA, sem limite de tempo. Vêm do `git commit` que a IA rodou (repo pelo `cd`/`-C`, commit confirmado no `git log`) ou da linha `[branch sha] assunto` + `N files changed` na saída das ferramentas (pega scripts como o backup do Kiro). Fontes: tool_use dos transcripts do
  Claude, sessões do kiro-cli (hora = prompt do turno) e `run_command` dos passos do
  Antigravity. Até 6 por IA. Substituiu a antiga tela Modelos (15/09/2026).
- Agenda: Google Calendar read-only (`google_agenda.py`, login em `google_calendar_login.py`).
- Cotações: CoinGecko, Yahoo, Fundamentus (`market_quotes.py`). Vasco: ESPN (`team_fixtures.py`).
- Fantasias do dia (`costumes.py`): Vasco em dia de jogo > Natal > Carnaval > Halloween.
- **Almirante** (mascote do Vasco): só em dia de jogo (`cos` = Vasco). Terceiro na fila depois do Kiro. No canto de
  todas as telas (células de 2 px, `MAS_ALMIRANTE`): parado → sai andando pela esquerda → aparece grande na borda direita
  comemorando → volta andando. No palco da tela inicial (`alm_on`) anda a rota do Kiro e pula ao voltar. Gol do Vasco na
  tela ao vivo: `splash_almirante_goal()` mostra ele grande pulando no canto inferior esquerdo por 7 s. Sprite gerado por
  `tools/make_almirante.py` a partir de `assets/almirante/almirante_recorte.png` → `firmware/src/almirante.h`.

## Simulador e vídeos
- `pio run -d firmware -e sim_demo` (tempos acelerados). Roteiro com `screen`/`tap`/`drag`/`quit`.
- Vídeo: `SDL_VIDEODRIVER=dummy SIM_SCENARIO=sim/demo.jsonl SIM_RECORD=demo.mp4 .pio/build/sim_demo/program`
  (rodar de dentro de `firmware/`).

## Histórico recente e decisões
- Modo Wi-Fi foi tentado e **revertido** (BLE ficava sem memória interna). Boot limpa as NVS `wifi`/`wificat`.
- GitHub: https://github.com/diasgthiago92/Clawdmeter (público, branch `meu-clawdmeter`, remote `github`;
  `origin` é o upstream). Commit + `git push github meu-clawdmeter` ao final de cada mudança.
- Ideias ainda não feitas: aprovar/negar comandos do Claude Code pela tela, semáforo de sessões, lançador de
  prompts, pomodoro com agenda, carteira de investimentos, Vasco completo.
