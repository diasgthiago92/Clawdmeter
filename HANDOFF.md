# Passagem de contexto — Clawdmeter (atualizado em 16/09/2026)

Leia primeiro as **Diretrizes do projeto** no topo do `CLAUDE.md` (valem sempre). Responder em português.

## Hardware e gravação
- Placa: **Guition/Sunton ESP32-4848S040** (ST7701 RGB 480×480, toque GT911, CH340 `/dev/cu.usbserial-*`,
  sem botões/bateria). Env `guition_4848s040`. **Não é** a Waveshare AMOLED-2.16.
- Gravar (~2–5 min, só 115200):
  `pio run -d firmware -e guition_4848s040 -t upload --upload-port $(ls /dev/cu.usbserial-* | head -1)`
- Serial: abrir a porta **reinicia a placa**. Comandos: `screen <nome>` (usage, agenda, history, actions, routines,
  crypto, stocks, fiis, vasco) e `screenshot` (RGB565 **little-endian**, ~40 s). O palco da tela inicial é desenhado
  direto no painel e não aparece no screenshot. Imagens do README em `docs/meu-clawdmeter/` (placa = captura real).
- BLE sobe antes da tela (evita boot loop por falta de memória).
- LVGL: pool interno 96 KB + 256 KB na PSRAM (`LV_MEM_POOL_EXPAND_SIZE`). Sem memória, `lv_obj_create`
  trava num assert em laço infinito.

## Daemon (macOS)
- launchd `com.user.claude-usage-daemon`. Reiniciar após mudar código:
  `launchctl kickstart -k gui/$(id -u)/com.user.claude-usage-daemon` · log `~/Library/Logs/claude-usage-daemon.out.log`
- Testes: `pytest` não está no venv; instale num dir temporário e rode com `PYTHONPATH` os arquivos
  `daemon/tests/test_*.py` (menos os de Windows/Linux).

## Telas (ordem de toque: direita avança, esquerda volta)
Segurar o dedo no centro por 2 s pausa/retoma a troca automática de telas; depois de 1 h pausado, retoma sozinho (16/09/2026).
Clawd (tela inicial) → **Consumo Atual** (painéis ordenados pelo % de uso, maior no topo; empate/sem dados segue Claude Weekly, Kiro Monthly, Gemini Weekly, Claude Daily, Gemini Daily, Codex Daily, Codex Weekly; só desliza sozinha se algum painel escondido tiver uso > 0%) →
**Consumo - 24 horas** (barras Claude/Gemini/Codex/Kiro) → **Agenda de Hoje** → **Rotinas Automáticas** → **Cronograma** (posts do Instagram e do TikTok) →
**Criptomoedas** → **Bovespa** (38 ações, P/VP) → **Juros Futuros** (curva DI1 inteira num gráfico, hoje × ajuste anterior; rodapé com o vencimento mais curto e o mais longo) → **Fundos Imobiliários** (30 FIIs) → **Jogos do Vasco**.
Telas de alerta que travam a rotação: **Reunião** (5 min antes, contagem; botão "Começar" abre o link Meet/Zoom/Teams no Mac via `{"mg":1}`), **Rotina falhou** (vermelha, botão
"Rodar de novo"), **Vasco ao vivo**.

## Fontes de dados (daemon/)
- Rotina **Preços celulares**: `kiro_routines.phone_row` lê `~/monitor-celulares/estado.json` e conta os logs do dia. Coleta às 11h (São Paulo), recuperação pelo próprio LaunchAgent a cada 15 min após falhas. Aparece mesmo sem Slack. Payload de rotina: status 0 erro/1 ok/2 exec/3 aguardando 11h; sexto campo opcional 1 identifica Codex (verde). Sem botão de reexecução: o controlador já gerencia as tentativas. Firmware atualizado interpreta esses estados; as rotinas antigas continuam usando 0/1.
- Claude: API de uso + transcripts (`usage_extras.py`). Gemini = Antigravity CLI (`antigravity_usage.py`, bancos
  em `~/.gemini/antigravity-cli/conversations`, formato reverso-engenheirado).
- Kiro: créditos do mês nos logs do Kiro IDE (real); 24h/5h = requisições do kiro-cli × crédito médio (estimado)
  (`kiro_usage.py`). Rotinas: Slack #leo-dias-news (`kiro_routines.py`, rerun só via lista fixa + launchctl).
- A tela **Últimas Ações** (commits das IAs, `ai_actions.py`) foi retirada em 17/09/2026 a pedido do usuário.
- Cronograma (`posts_schedule.py`, payload `{"p": [[rede, "DD/MM HHh", título, estado]]}`, rede 0 Instagram/1 TikTok, estado 0 agendado/1 enviado/2 não enviado): lê o projeto `~/.claude/projetos/eaiproduto` só para leitura, reaproveitando `dashboard.cronograma(agora, dias_atras)` (só hoje e amanhã, sem stories; a tela só redesenha quando o conteúdo muda). TikTok = reels; "enviado" vem do log `logs/<slot>.log` (`TikTok: rascunho enviado`) ou do `enviado_em` de `content/tiktok-fila.json`. Logos em `firmware/src/social_icons.h`, gerados por `tools/make_social_icons.py` (ImageMagick).
- Agenda: Google Calendar read-only (`google_agenda.py`, login em `google_calendar_login.py`).
- Cotações: CoinGecko, Yahoo, Fundamentus (`market_quotes.py`). Juros futuros: API pública da B3 `cotacao.b3.com.br/mds/api/v1/DerivativeQuotation/DI1` (`RateQuotes`, payload `{"j": [[aamm, taxa, ajuste anterior]]}` em milésimos de %). Vasco: ESPN (`team_fixtures.py`).
- Fantasias do dia (`costumes.py`): Vasco em dia de jogo > Natal > Carnaval > Halloween.
- **Almirante** (mascote do Vasco): só de 2 h antes do início do jogo até o fim do dia (`{"alm": 1}`, `almirante_window` em `team_fixtures.py`); a camisa do Vasco no Clawd e no Kiro continua valendo o dia inteiro. Terceiro na fila depois do Kiro. No canto de
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
  `origin` é o upstream). Commit ao final de cada mudança e push **nos dois repos** (pedido do usuário em 15/09/2026):
  `git push github meu-clawdmeter` **e** `git push lgtm meu-clawdmeter` (https://github.com/goncalvesdthiago-lgtm/clawdmeter).
- Ideias ainda não feitas: aprovar/negar comandos do Claude Code pela tela, semáforo de sessões, lançador de
  prompts, pomodoro com agenda, carteira de investimentos, Vasco completo.
