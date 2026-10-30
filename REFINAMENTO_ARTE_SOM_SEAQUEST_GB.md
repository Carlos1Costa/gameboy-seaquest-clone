# Refinamento de Arte & Som — Seaquest GB (fidelidade ao original)

> **Para:** Claude Code
> **De:** Carlos
> **Complemento de:** `GUIA_SEAQUEST_GB.md` (spec de referência)
> **Status do projeto:** base de gameplay (EPICs A–F) implementada e testada (107 testes passando no PyBoy). Esta rodada é a camada de **animação, arte e som** que ainda não existe no código.
>
> Idioma: português para conversa; **código/nomes/comentários em inglês** (convenção do guia principal). Mantemos os princípios: poucos arquivos coesos, sem macros (usar `enum`/`const`/`static inline`), simplicidade acima de esperteza.

---

## 0. Onde o projeto está (lido do repositório)

Para esta rodada não começar no vácuo, segue o estado atual real do código (`src/game.c`, `src/game.h`, `src/sound.c`, `res/`):

- **Entidades e render já existem** e são sólidos. Cada entidade é desenhada por `render_entity(first_tile, x, y, facing_left, sprite)` em `game.c`, que coloca **2 tiles 16×8** (`first_tile` e `first_tile+1`), com espelhamento por hardware (`S_FLIPX` + troca da ordem dos tiles). Não há scroll; HUD vive no background.
- **Cada sprite tem só 1 frame hoje.** Os PNGs em `res/` são todos **16×8 (2 tiles)**, frame único: `player_sub.png`, `enemy_sub.png`, `shark.png`, `diver.png`, `explosion.png`, `torpedo.png` (8×8). O pipeline no `Makefile` chama `png2asset ... -spr8x8 -tiles_only` e gera, por PNG, `<nome>_tiles[]` + `<nome>_TILE_COUNT`.
- **Tabela de tiles de sprite** é um `enum SpriteTile` em `game.c` que encadeia os offsets: `SPRITE_TILE_PLAYER_SUB = 0`, o próximo começa em `anterior + <nome>_TILE_COUNT`, etc. **Adicionar frames muda esses offsets automaticamente** — ótimo, só precisamos respeitar o padrão.
- **`game->frame`** (um `uint8_t` que faz wrap) já existe e já serve de clock para o alarme de O₂ e a onda do tubarão (`shark_wave[]`, lookup `const` — nada de `sin()` em runtime, exatamente como queríamos). **É o clock natural para as novas animações.**
- **Som:** `sound.c`/`sound.h` têm 7 efeitos one-shot (`SOUND_TORPEDO`, `SOUND_ENEMY_EXPLOSION`, `SOUND_PLAYER_EXPLOSION`, `SOUND_RESCUE`, `SOUND_OXYGEN_ALARM`, `SOUND_TALLY`, `SOUND_EXTRA_LIFE`), cada um uma escrita nos registradores da APU. Canais: pulse1 (torpedo/extra life, com sweep), pulse2 (bips), noise (explosões).

### 0.1. O que esta spec pede (resumo do que FALTA)
1. **Animação de todas as entidades** (hélices, mordida do tubarão, chute+bolhas do diver). Hoje inexistente.
2. **Som de recarga de oxigênio** — pedido explícito do Carlos; **não existe** `SOUND_OXYGEN_REFILL`. O `update_refill()` enche o tanque em silêncio.
3. **Correção do HUD: o texto na tela ainda é `"O2"`** (linha `draw_text(OXYGEN_LABEL_COLUMN, ROW_OXYGEN, "O2")` em `game.c`). O Carlos quer **`OXYGEN`** como no original. (O README já diz "oxygen", mas o que o jogador vê é "O2".)
4. **Arte mais fiel/detalhada** (herói mais bonito que inimigos; pose correta do diver), dentro do que o GB permite.

Lembrete de tons do DMG (claro→escuro): **T0** branco (transparente p/ sprite), **T1** cinza claro, **T2** cinza escuro, **T3** preto. No GB não há cor — diferenciamos por **forma, dithering e tom**.

---

## 1. Infra de animação (a fundação — fazer primeiro)

Toda animação no GB é **flipbook de tiles**: troca-se qual tile o sprite exibe. O encaixe no código atual é mínimo e cirúrgico:

### 1.1. Como encaixa no render existente
Hoje `render_entity(first_tile, ...)` recebe o tile-base fixo da entidade. Para animar, a entidade passa a ter **N frames** no PNG (lado a lado), e o render soma um **offset de frame**:

```
tile_do_frame = SPRITE_TILE_<ENTIDADE> + frame_index * tiles_por_frame
```

Com entidades 16×8, `tiles_por_frame = 2`. Sugestão: uma pequena `static inline` que calcula o tile-base do frame atual, mantendo `render_entity` quase intacto (recebe o `first_tile` já deslocado, ou um parâmetro `frame`). **Sem macro** — função inline resolve e o debugger enxerga.

### 1.2. Cadência (não animar a 60fps)
Reusar `game->frame`. O índice do frame de animação sai de uma divisão/máscara barata sobre `game->frame`, por entidade:

```c
// Cadence per entity: swap frame every N game frames. enum, not macro.
enum AnimTuning {
    ANIM_FRAMES_PROPELLER = 6,   // hero/enemy/patrol sub propeller
    ANIM_FRAMES_SHARK     = 10,  // shark tail + bite
    ANIM_FRAMES_DIVER     = 12,  // diver kick + bubbles
    ANIM_PROPELLER_COUNT  = 2,
    ANIM_SHARK_COUNT      = 2,
    ANIM_DIVER_COUNT      = 2
};

// frame index for a 2-frame cycle; cheap on the SM83 (no divide if count is a power of two)
static inline uint8_t anim_frame_2(uint8_t clock, uint8_t frames_per_step) {
    return (clock / frames_per_step) & 1;
}
```

Observações de CPU (coerentes com o estilo do projeto):
- Preferir `count` potência de 2 para o índice virar máscara (`& (count-1)`), evitando divisão real por frame.
- Sharks de uma mesma lane compartilham `game->frame`, então animam em fase — aceitável e barato. Se quiser quebrar a sincronia, somar o índice do membro antes da máscara.
- Animação é **cosmética**: não deve tocar em colisão, spawn ou qualquer coisa testada. Assim os 107 testes continuam válidos (eles leem posição/estado, não o tile exibido).

### 1.3. Orçamento de VRAM após adicionar frames
Somando 2º frame a sub do herói, inimigo, tubarão e diver (patrol reusa enemy; torpedo e explosão como estão ou +frames na explosão):

| Entidade | Frames | Tiles (16×8 = 2/f) |
|---|---|---|
| player_sub | 2 | 4 |
| enemy_sub | 2 | 4 |
| shark | 2 | 4 |
| diver | 2 | 4 |
| explosion (opcional 3 frames) | 3 | 6 |
| torpedo | 1 | 1 |
| **Total sprites** | | **~23 tiles** |

Folga confortável nos 384 tiles totais (compartilhados com o background, que usa `hud_tiles.png`). Mantemos `-noflip` no png2asset e seguimos espelhando por hardware — **não** gerar tiles espelhados.

---

## 2. Entidades — arte e animação

> Importante sobre os PNGs: cada frame continua **16×8**, colocados **lado a lado** no mesmo PNG (frame 0 à esquerda, frame 1 à direita, ...). O `png2asset` com `-spr8x8 -tiles_only` já lê a fileira inteira de tiles na ordem certa; só precisamos manter a **ordem dos tiles por frame** consistente com o cálculo da seção 1.1. Ver seção 4 sobre o Makefile.

### 2.1. Hero sub (player_sub.png) — "o bonito"
Original: amarelo, corpo alongado, **torre de comando** elevada no topo-centro, nariz arredondado, **cauda com 3 pontas de propulsão** que vibram. Nitidamente mais elaborado.

No GB:
- **Casco:** T1 (claro), com faixa T2 na linha inferior para volume.
- **Torre:** bloco elevado T2 no topo-centro + 1 px T0 de vigia/periscópio. **Esse vigia é o detalhe que o inimigo não tem.**
- **Nariz:** cantos cortados (arredondado), T1.
- **Propulsão (anim, 2 frames):** as pontas traseiras alternam posição → hélice/jato. Roda **sempre**, até parado (motor nunca desliga).
- `facing`: esquerda = flip X por hardware (já suportado no render). A orientação do sub já existe no `Player.facing_left`.

### 2.2. Enemy sub (enemy_sub.png) — "o simples"
Original: cinza, silhueta parecida mas **mais achatada e sem detalhes**; torre é um bloquinho só. Atira torpedo.

No GB:
- **Casco:** T2 (escuro) — já o deixa "mais pesado/ameaçador" e o separa do herói (T1).
- **Torre:** bloco chato, **sem vigia**. Casco liso, **sem janelas**.
- **Propulsão (anim, 2 frames):** hélice mais tosca (1 px mexendo).
- `facing`: flip X por hardware.

> **Contraste herói×inimigo sem cor:** herói = T1 claro + torre com vigia + cauda trabalhada; inimigo = T2 escuro + torre lisa. A leitura "mocinho vs vilão" fica clara em P&B.

### 2.3. Patrol sub
No código já **reusa o tile do enemy sub** no render (`render_entity(SPRITE_TILE_ENEMY_SUB, patrol.x, PATROL_Y, ...)`). **Manter o reuso** — ganha a hélice do enemy sub de graça. Nada novo de arte salvo se quisermos destacá-lo (não prioritário).

### 2.4. Shark (shark.png)
Original: muda de cor por wave, nada horizontal **oscilando em onda**, com **cauda/boca animando** (morde ao nadar). A onda já está implementada (`shark_wave[]` + `SHARK_WAVE_*`) — **não mexer nisso**, é trajetória, não tile.

No GB (só a parte de tile/arte):
- **Corpo:** T2 com barriga T1 (duas faixas) para volume; barbatana dorsal T2 no topo.
- **Anim (2 frames):** boca fecha/abre (1–2 px T0 na frente) **e** cauda sobe/desce — pode ser o mesmo par de frames (morde e bate cauda juntos), cadência `ANIM_FRAMES_SHARK`.
- Diferenciação de wave sem cor (opcional, baixa prioridade): variar **dithering** do corpo conforme a wave. Simplificação aceitável: manter o sprite e deixar a dificuldade falar pela velocidade.
- `facing`: flip X por hardware (já conforme direção de entrada da lane).

### 2.5. Diver (diver.png)
Pedido específico do Carlos, confirmado no vídeo: nada **inclinado (corpo em diagonal) com cabeça/tronco erguidos**, pernas estendidas atrás chutando, **borbulhando** enquanto nada.

No GB:
- **Pose:** corpo em diagonal ~30–45° subindo da perna (trás-baixo) à cabeça (frente-cima). Cabeça = "bolinha" T2 na ponta superior-frontal; tronco T1; pernas = 2 px T2 atrás.
- **Anim chute (2 frames):** pernas abrem/juntam → nado.
- **Bolhas:** 1–2 px T1 saindo atrás/acima da cabeça, subindo e sumindo. Cabe dentro do tile do diver (não gastar sprite extra) — alternar no mesmo par de frames do chute.
- `facing`: flip X conforme direção. **Testar o espelhamento:** a pose "cabeça erguida" deve continuar correta virada para o outro lado (não pode ficar de cabeça pra baixo).

### 2.6. Torpedos / Explosão
- **Torpedos:** como estão (player T1, 1 tile). Opcional: enemy torpedo com 1 px piscando para leitura de perigo.
- **Explosão:** hoje é 1 frame reaproveitado para qualquer destruição. **Upgrade opcional (bom custo/benefício):** 2–3 frames (ponto → estrela → nuvem, alternando T1/T2/T3) ao longo de `DEATH_FRAMES`/`EXPLOSION_BLINK_FRAMES`, que já existem. Aumenta o "juice" sem lógica nova.

---

## 3. HUD e cenário

### 3.1. `O2` → `OXYGEN` (correção pedida pelo Carlos)
O código desenha `"O2"` em `draw_static_background()` (`game.c`). Trocar para `"OXYGEN"`.

Atenção ao **espaço**: hoje o label ocupa de `OXYGEN_LABEL_COLUMN` (=2) e a barra começa em `OXYGEN_BAR_COLUMN` (=5), ou seja, só **3 colunas** para o texto — cabe "O2" mas **não** "OXYGEN" (6 letras). Opções, em ordem de preferência:
1. **Mover a barra para a direita:** aumentar `OXYGEN_BAR_COLUMN` para ~9 e reduzir `OXYGEN_BAR_TILES` para caber na largura de 20 tiles (checar que `BAR_COLUMN + BAR_TILES <= 20`). Ajustar os testes de oxigênio que assumem posição/contagem de tiles da barra.
2. Abreviar para algo intermediário se o espaço não fechar (mas o pedido é "OXYGEN" como no original — preferir opção 1).

Confirmar que as letras O, X, Y, G, E, N existem em `res/hud_tiles.png`/`letter_glyphs` (há um `BG_TILE_LETTER_FIRST`; validar o conjunto; se faltar alguma, adicionar o glifo ao PNG do HUD).

### 3.2. Barra de O₂
Já funciona (`BG_TILE_O2_FULL`/`BG_TILE_O2_EMPTY`, pisca no `OXYGEN_LOW`). Manter. O "vermelho" do original vira T3/dithering no segmento crítico (já há blink de alarme).

### 3.3. Indicadores (já implementados)
Subs reserva (`draw_icon_counter` na `ROW_LIVES`) e fileira de divers (`ROW_DIVERS`) já existem e batem com o original. Nada a fazer além de, se desejar, usar um ícone de sub mais caprichado coerente com a nova arte do herói.

---

## 4. Pipeline de assets (Makefile) — atenção ao animar

Ao transformar um PNG de 1 frame (16×8) em vários frames lado a lado (ex.: 32×8 para 2 frames), o comando atual já funciona em princípio, porque `-spr8x8 -tiles_only` lê todos os tiles da fileira em ordem. Pontos a garantir:
- A **ordem dos tiles** gerada deve ser: `[frame0_tileL, frame0_tileR, frame1_tileL, frame1_tileR, ...]`. Com `-spr8x8` a leitura é por colunas de 8px da esquerda para a direita — **validar** que o `_tiles[]` sai nessa ordem (um teste rápido ou inspeção do `.c` gerado em `build/assets/`). Se a ordem vier diferente, ajustar a montagem do PNG (dispor frames na ordem que o png2asset espera) em vez de criar macro/gambiarra no C.
- `<nome>_TILE_COUNT` passa a refletir todos os frames — e como o `enum SpriteTile` encadeia offsets por `TILE_COUNT`, **os offsets das entidades seguintes se ajustam sozinhos**. Nada a mudar no enum.
- Manter `-noflip` (espelhamento é por hardware).
- Não é preciso `-c`/metasprite: o projeto desenha os 2 tiles manualmente no `render_entity`. Manter essa simplicidade (coerente com "poucos arquivos, sem indireção desnecessária").

---

## 5. Som — adicionar o refil de oxigênio (pedido do Carlos) + revisão

### 5.1. `SOUND_OXYGEN_REFILL` — o SFX "com estado"
Diferente dos outros: tem **início e fim** (começa ao tocar a superfície, para ao encher). O ponto de integração já existe: `update_refill()` em `game.c` (estado `PLAYER_REFILLING`).

Abordagem recomendada, mantendo o estilo one-shot do `sound.c` e **sem canal travado continuamente**:
- Tocar um **tick/borbulha curto a cada N unidades de oxigênio** reabastecidas (reusar a ideia do `TALLY_TICK_UNITS`), com pitch **subindo** conforme o tanque enche — dá a sensação de "enchendo em degraus" sem precisar de som contínuo nem de parar um canal no fim.
- Som distinto do `SOUND_TALLY` e do `SOUND_RESCUE` (timbre/pitch diferentes) para não confundir.
- Canal: pulse1 ou pulse2 (ver 5.2). Como o alarme de O₂ (submerso) e o refil (superfície) **nunca coexistem**, podem dividir canal sem conflito.

Se preferirmos som **verdadeiramente contínuo** (borbulhar sustentado), aí sim precisaria de `sound_start_oxygen_refill()` / `sound_stop_oxygen_refill()` com um canal dedicado ligado/desligado — mais fiel a um "borbulhar", porém com estado de canal para gerenciar. **Recomendo começar pela versão em degraus** (mais simples, casa com a infra atual); se no playtest soar pobre, migrar para a contínua. Decisão do Carlos no teste.

Novo item no enum de `sound.h`: `SOUND_OXYGEN_REFILL` (e, se formos para a versão contínua, as duas funções start/stop em vez de um enum).

### 5.2. Mapa de canais (com o novo som)
| Evento | Canal | Caráter |
|---|---|---|
| Tiro do player | pulse1 (sweep) | "pew" curto descendente (já existe) |
| Explosão inimigo/player | noise | ruído com envelope (já existe) |
| Resgate de diver | pulse2 | blip ascendente (já existe) |
| Alarme O₂ baixo | pulse2 | beep ~440 Hz (já existe) |
| Tally (contagem no surface) | pulse2 | tick alto suave (já existe) |
| Extra life | pulse1 (sweep) | sweep ascendente (já existe) |
| **Refil de oxigênio (NOVO)** | pulse2 | tick borbulhante, pitch subindo em degraus; some ao encher |

Nenhuma música de fundo (decisão do Carlos). Sobra o canal **wave (CH3)** livre — reserva para o futuro, se quisermos um borbulhar contínuo no refil sem disputar o pulse2.

### 5.3. Teste
`tests/test_sound.py` verifica **qual efeito cada evento solicita**. Ao adicionar o refil, incluir um caso: durante `PLAYER_REFILLING`, o jogo pede `SOUND_OXYGEN_REFILL` na cadência esperada e **para** quando o tanque enche. Mantém a cobertura no mesmo padrão dos outros SFX.

---

## 6. Backlog desta rodada (EPICs novos para o guia principal)

**EPIC G — Arte e animação**
- G1.1: Infra de animação por tile (offset de frame no `render_entity` + cadência via `game->frame`, `enum`/`inline`). *AC: hero sub mostra hélice girando; cadência ajustável por constante; 107 testes seguem verdes.*
- G1.2: Hero sub detalhado (torre, vigia, cauda animada 2 frames). *AC: visivelmente distinto do enemy sub.*
- G1.3: Enemy sub detalhado (T2, torre lisa, hélice 2 frames); patrol reusa. *AC: lê como inimigo; patrol herda a hélice.*
- G1.4: Shark com boca/cauda animada (2 frames). *AC: morde ao nadar; onda intacta (sem mexer em `shark_wave`).*
- G1.5: Diver (pose inclinada cabeça-erguida, chute + bolhas, 2 frames). *AC: pose correta ao espelhar; borbulha.*
- G1.6: Explosão multi-frame (opcional). *AC: 2–3 frames ao longo de DEATH_FRAMES.*
- G1.7: HUD — `O2` → `OXYGEN` (reposicionar barra; validar glifos). *AC: tela mostra "OXYGEN"; testes de O₂ ajustados e verdes.*

**EPIC H — Áudio (expande F1.3)**
- H1.1: `SOUND_OXYGEN_REFILL` (versão em degraus, pitch subindo). *AC: toca durante `PLAYER_REFILLING`, para ao encher; distinto de tally/rescue.*
- H1.2 (opcional): migrar para refil contínuo com start/stop no canal wave, se o playtest pedir.

---

## 7. Ordem de execução sugerida

1. **G1.7** primeiro: `O2` → `OXYGEN` (fecha um pedido do Carlos e é rápido; mexe no HUD/testes, melhor isolar antes das animações).
2. **G1.1** (infra de animação) — fundação; sem ela nada anima.
3. Animar por impacto visual: **G1.2 hero sub → G1.4 shark → G1.5 diver → G1.3 enemy/patrol**.
4. **H1.1** som de refil de oxigênio.
5. **G1.6** explosão multi-frame (polish).

Cada passo deve manter os **107 testes verdes** (animação é cosmética; som novo ganha seu próprio teste). Rodar `make test` ao fim de cada item.

Fonte: análise de frames do gameplay original (Atari 2600, 1983) fornecido pelo Carlos + leitura do repositório `gameboy-seaquest-clone` (estado em 2026-10-30) + `GUIA_SEAQUEST_GB.md`.