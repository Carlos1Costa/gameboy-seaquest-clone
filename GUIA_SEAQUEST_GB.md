# Guia de Desenvolvimento — Seaquest para Game Boy (GBDK-2020 / C)

> **Para:** Claude Code
> **De:** Carlos
> **Objetivo:** Construir, em colaboração, um clone de *Seaquest* (Activision, Atari 2600, 1983) para o Game Boy original (DMG), escrito em C com GBDK-2020.
>
> Este documento é a nossa **spec de referência**. Vamos conversar em português, mas **todo o código — nomes de variáveis, funções, comentários, commits — é em inglês.**

---

## 1. Como vamos trabalhar juntos (convenções)

Carlos definiu princípios de engenharia que valem para **todo** o projeto. Siga-os à risca:

### 1.1. Organização de arquivos: poucos arquivos, não muitos
- **Não fragmentar o código em dezenas de arquivos pequenos.** Isso dificulta manutenção e entendimento.
- Preferência por **poucas unidades de compilação coesas**, cada uma com responsabilidade clara.
- Um arquivo só "nasce" quando há um motivo real (um subsistema grande e independente), não por reflexo de "um arquivo por struct".
- Veja a estrutura proposta na seção 6. É deliberadamente enxuta.

### 1.2. Sem macros (salvo exceção justificada)
- **Evitar macros em C.** Macros tornam o debug extremamente difícil (o debugger não as enxerga, erros de expansão são opacos).
- Em vez de macros, prefira:
  - `const` / `enum` para constantes (têm tipo e aparecem no debugger).
  - `static inline` functions para "macros de função" — têm checagem de tipo e são depuráveis.
- **Exceção aceitável:** apenas quando a macro é genuinamente superior e inevitável — p. ex. um guard de include, ou algo que *só* funciona como macro (como `BANKED`/atributos do próprio GBDK que a lib exige). Se propuser uma macro, **justifique por que não dá para ser `const`/`enum`/`inline`.**

### 1.3. Simplicidade acima de esperteza
- Código claro e linear vence código "inteligente". Estamos num hardware de 8 bits; previsibilidade importa.
- Nomes descritivos em inglês. Sem abreviações crípticas.
- Comentários explicam o **porquê** (decisão de design, constraint de hardware), não o **o quê** óbvio.
- Funções curtas e com propósito único, mas sem criar indireção desnecessária.
- Antes de otimizar, medir. Em C com SDCC, **CPU também é gargalo** (ver seção 6.1) — não só VRAM/sprites por linha.

### 1.4. Testes acompanham o código
- O projeto mantém uma suíte de testes própria em `tests/`, que roda a ROM real no emulador PyBoy, sem janela. Guia completo em [tests/README.md](tests/README.md).
- **Toda feature ou correção entra com seus testes**, no mesmo commit. Bug corrigido ganha um teste que falharia antes da correção.
- `make test` precisa passar antes de cada commit.
- Um teste novo deve ser visto falhando ao menos uma vez (quebrando o código de propósito): teste que não consegue falhar não protege nada.
- Testes não fixam valores de tuning: leem de `game.h` via `tests/layout_probe.c`. Ajustar um número no playtest não deve quebrar a suíte.

---

## 2. Plataforma-alvo e restrições de hardware

Alvo: **Game Boy original (DMG)** — 4 tons de cinza. (Portar para GBC pode vir depois; projetar pensando nisso, mas não implementar cor agora.)

| Recurso | Limite | Impacto no Seaquest |
|---|---|---|
| CPU | Sharp LR35902 ~4.19 MHz | Loop de jogo precisa caber em 1 frame (~16.7 ms) |
| Resolução | 160 × 144 px | Tela de jogo + HUD de oxigênio/divers/score |
| Tiles | 8×8 px, até 384 em VRAM | Orçamento de tiles é escasso; reusar bastante |
| Sprites | **Máx 40 na tela, máx 10 por scanline** | **Constraint crítico** (ver 2.1) |
| Tamanho sprite | 8×8 ou 8×16 px (modo global) | **Decidido: 8×8.** Sub, inimigos e divers são entidades 16×8 (2 sprites 8×8 lado a lado); torpedos usam 1 sprite 8×8 |
| Som | 4 canais (2 pulse, 1 wave, 1 noise) | Só SFX, sem música (decisão do Carlos) |

### 2.1. O constraint dos 10 sprites por scanline
No Game Boy, **só 10 sprites podem ser desenhados na mesma linha horizontal**; o 11º some. Como no Seaquest os inimigos vêm em **fileiras horizontais**, dois ou mais inimigos na mesma altura Y podem estourar o limite junto com o sub e divers.
- **Mitigação:** limitar o número de entidades ativas por "faixa" (row) vertical. Projetar o spawner para distribuir inimigos entre as fileiras.
- **Orçamento adotado:** 1 sub do player (2 hardware sprites, entidade 16×8) + torpedo (1), **até 8 inimigos** (`MAX_ACTIVE_ENEMIES`) e **até 2 torpedos inimigos** (`MAX_ENEMY_TORPEDOES`) simultâneos, divers conforme as fileiras, 1 patrol sub (EPIC F). Os tetos vêm do orçamento de **CPU** (seção 6.1), não do de sprites.
- **Conta por scanline:** cada entidade 16×8 ocupa **2 sprites** na linha em que está. Pior caso: 3 entidades da fileira (6) + torpedo inimigo da fileira (1) + sub do player (2) + torpedo do player (1) = **10** — exatamente o limite. Por isso o spawner garante **no máx. 3 entidades 16×8 por fileira** (inimigos + diver) e **1 torpedo inimigo por fileira**.
- **Modelo de fileiras (implementado no EPIC C):** 4 fileiras fixas na água, espaçadas 22 px. Cada fileira recebe um *grupo*: 1–3 inimigos do mesmo tipo (sharks **ou** enemy subs), mesma direção, entrando juntos pelo mesmo lado, mais um diver opcional no fim da fila (com diver, o grupo tem no máx. 2 inimigos). Uma fileira só recebe novo grupo depois de esvaziar, e só se o teto global de inimigos permitir. O grupo se move como uma unidade: guarda-se só o x do líder e uma máscara de bits de membros vivos (`alive_mask`); cada membro fica a 24 px do anterior e todos compartilham o mesmo y (o grupo de sharks oscila em sincronia, como no original). O espaçamento garante que um shark oscilando (±4 px) nunca invade a fileira vizinha, então as fileiras não somam sprites entre si.
- **Prioridade:** o player é desenhado nos primeiros slots de OAM; se algum dia uma linha estourar, quem some é um inimigo, nunca o sub.
- **Por que 8×8 e não 8×16:** o modo 8×16 gera sprites altos e estreitos (8 px de largura); sub, tubarões e divers são largos e baixos, então 16×8 (dois 8×8 lado a lado) representa melhor as formas sem desperdiçar tiles.

---

## 3. O jogo: descrição completa de gameplay

### 3.1. Premissa
O jogador pilota um submarino para **resgatar mergulhadores (divers)** no fundo do oceano, **gerenciando oxigênio** e **combatendo/desviando de inimigos**. Visão lateral, tela fixa (fixed/flip screen — o cenário não faz scroll).

### 3.2. Loop central
1. Submergir e nadar pelo oceano.
2. Resgatar divers encostando neles (carrega até **6** por vez).
3. Atirar torpedos em tubarões e subs inimigos.
4. Vigiar a barra de oxigênio, que drena continuamente.
5. Subir à superfície para reabastecer oxigênio e descarregar divers.
6. Repetir em ondas (waves) progressivamente mais difíceis.

### 3.3. Movimento e controles
- **Submarino do player:** move-se livremente em **todas as direções** (cima, baixo, esquerda, direita) por qualquer lugar da tela.
- **D-Pad:** movimento nas 4 direções (com diagonais).
- **A:** disparar torpedo.
- O sub tem **orientação** (aponta para esquerda ou direita conforme a última direção horizontal); o torpedo sai na direção para onde o sub aponta.
- Torpedos são **ilimitados** (pode segurar/repetir o tiro). Sugiro limitar a **1 torpedo do player na tela por vez** (como o original tende a se comportar), para equilíbrio e economia de sprites. A confirmar no playtest.

### 3.4. Entidades (personagens/objetos)

| Entidade | Comportamento |
|---|---|
| **Player sub** | Controlado pelo jogador; movimento livre; dispara torpedos; 1 toque de qualquer inimigo = morte. |
| **Diver (mergulhador)** | Entra pela esquerda ou direita, atravessa horizontalmente. Resgatado por contato com o sub. Max 6 a bordo. |
| **Shark (tubarão)** | Entra pela esquerda ou direita, move-se horizontalmente **mas oscila para cima/baixo em padrão ondulado** (mais difícil de desviar). **Não atira.** Destruído por torpedo. |
| **Enemy sub (submarino inimigo)** | Entra pela esquerda ou direita, move-se horizontalmente. **Atira torpedos no player.** Destruído por torpedo. |
| **Enemy torpedo** | Projétil disparado pelo enemy sub; mata o player ao contato. |
| **Patrol sub (sub patrulha)** | Aparece na linha da superfície após as primeiras waves. Patrulha o topo e pode **ramar o player enquanto ele está congelado reabastecendo oxigênio.** Perigo de 1 toque. |
| **Player torpedo** | Projétil do player; destrói sharks e enemy subs. |

> Inimigos e divers **aparecem em grupos/fileiras** e surgem de ambos os lados. Sharks podem não atirar, mas o padrão ondulado compensa.

### 3.5. Regras de superfície (surfacing) — o coração do jogo
- **Carga cheia (6 divers):** subir descarrega todos com segurança, **converte o oxigênio restante em pontos de bônus**, limpa inimigos padrão e avança para uma wave mais difícil.
- **Carga parcial (1 a 5 divers):** subir cedo **custa 1 diver** por viagem antecipada (você perde um dos resgatados).
- **Carga vazia (0 divers):** subir sem nenhum diver **destrói o submarino imediatamente** (perde vida).
- **Refil congelado:** durante o reabastecimento de oxigênio o player **fica parado**; a partir de certas waves, o **patrol sub** continua se movendo e pode te matar nesse momento.
- **Ficar sem oxigênio:** destrói o sub. Como em toda morte, perde a vida **e todos os divers a bordo** (decidido: fiel ao original).

### 3.6. Oxigênio
- Barra drena a taxa constante enquanto submerso.
- Enche ao atingir a superfície.
- Oxigênio restante em surface com carga cheia → bônus (ver scoring).

### 3.6.1. Como foi implementado (EPIC D)
- **Estados do player:** `PLAYER_REFILLING` (na superfície, congelado, O₂ enchendo), `PLAYER_ACTIVE` e `PLAYER_DYING` (explosão piscando por 90 frames ≈ 1,5 s, com o mundo congelado).
- **Toda vida começa na superfície com o tanque vazio**, enchendo (~1,6 s), como no original. Isso também evita que o início do jogo conte como "subir com 0 divers".
- **Submerso** = casco inteiro abaixo da linha d'água (`PLAYER_SUBMERGED_Y`, 8 px abaixo da superfície). **Subir** = voltar à superfície depois de ter submergido; balançar rente à superfície não aciona as regras.
- O₂ **só drena submerso** (1 unidade a cada 10 frames; tanque cheio ≈ 16 s). Com 2 blocos ou menos, a barra **pisca** (alarme visual; o sonoro vem no F1.3).
- **Regras ao subir:** 6 divers → descarrega todos, limpa o oceano, `wave++` e reabastece; 1–5 → perde 1 diver e reabastece; 0 → explode.
- **Mortes** (O₂ zerado, colisão com inimigo/torpedo ou subir vazio): perde a vida **e todos os divers a bordo**, como no original. Após a explosão: perde uma reserva e reaparece na superfície; sem reservas, o jogo reinicia (placeholder até o fluxo de game over do E1.3).

### 3.7. Sistema de pontuação
> **Nota:** há divergência entre fontes sobre o valor inicial de inimigos (a descrição do Carlos diz 20; a Wikipédia menciona 10, aumentando conforme avança). Vamos adotar os valores da descrição do Carlos como **canônicos para o nosso clone** e deixá-los em constantes fáceis de ajustar no playtest.

- **Inimigos (shark/enemy sub):** começam em **20 pts**. Cada conjunto completo de 6 divers trazido à superfície aumenta o valor dos inimigos em **+10** (até o máximo de **90 pts** cada).
- **Divers:** começam em **50 pts** cada. Cada conjunto completo surfado aumenta o valor do diver em **+50** (até o máximo de **1.000 pts** cada).
- **Bônus:** pontos extras proporcionais ao oxigênio restante ao surfar com carga cheia.
- **Vidas extras:** ganha um sub reserva a cada **10.000 pts** (até no máx **6** reservas). Começa com 1 ativo + 3 reservas.

### 3.7.1. Como foi implementado (EPIC E)
- **Score em dígitos decimais**, somado dígito a dígito (sem divisões de 32 bits); trava em 999999 em vez de voltar a zero.
- **Inimigo abatido:** `20 + 10 × wave` pontos (máx. 90). **Divers** são pagos ao subir com carga cheia: `6 × (50 + 50 × wave)` (máx. 1000 cada), usando o valor da wave **antes** de avançá-la. Subir com carga parcial não paga nada. *(Bônus de O₂ é o F1.2.)*
- **Vida extra** a cada 10.000 pontos (quando a dezena de milhar muda), até 6 reservas.
- **Game over:** ao perder a última vida, o mundo congela, os sprites somem e uma faixa "GAME OVER / PRESS START" cobre a água; START recomeça do zero. A tela de título e o polimento ficam no F1.4.

### 3.8. Dificuldade / waves
- Completar uma wave (surfar com 6) → inimigos mais rápidos, valores de ponto maiores, e eventualmente o patrol sub na superfície.
- Progressão clássica "só fica mais rápido e mais cheio". Sem fim definido — joga até perder todas as vidas.
- **Implementado (E1.4):** a velocidade dos inimigos sobe 1/8 px/frame por wave (`ENEMY_SPEED_PER_WAVE`), até 2 px/frame; a quantidade fica limitada pelos tetos de CPU (seção 2.1). Divers e torpedos mantêm a velocidade.

---

### 3.9. Como foi implementado (EPIC F)
- **Patrol sub:** patrulha a linha da superfície a partir da **wave 2** (`PATROL_FIRST_WAVE`), uma passada por vez, alternando o lado de entrada, com pausa aleatória de 1,5–2,5 s entre passadas. É **indestrutível** (o torpedo passa direto) e mata ao toque, inclusive durante o reabastecimento e a contagem. Cada nova vida ganha uma pausa antes da próxima passada. Usa o sprite do enemy sub.
- **Contagem ao subir com 6 (bônus de O₂):** estado `PLAYER_TALLYING` — o oceano é limpo, cada diver sai do HUD somando seus pontos (um a cada 12 frames), depois a barra de O₂ esvazia somando **10 pts por unidade** (tanque cheio = 960), e só então a wave avança e o tanque reabastece.
- **Efeitos sonoros** (`sound.c`): tiro, explosão de inimigo, explosão do sub, resgate, alarme de O₂ (bipe a cada piscada da barra), tique da contagem e vida extra. Cada efeito é uma escrita nos registradores do APU; o hardware cuida do envelope e da varredura, então não há custo por frame.
- **Telas:** `main.c` tem a máquina de estados **título → jogo → game over → título**, tudo com START. As telas são faixas de texto sobre o oceano congelado, sem sprites. A semente aleatória vem do tempo que o jogador passa no título (mais o registrador DIV), então cada partida é diferente.

## 4. Escopo do MVP vs. versões futuras

Para não afogar o projeto (trocadilho intencional 🌊), fatiamos em entregas jogáveis.

### MVP (primeira versão jogável end-to-end)
- Sub do player com movimento livre e 1 torpedo.
- Sharks (movimento ondulado) + enemy subs (que atiram).
- Divers resgatáveis, contador até 6.
- Barra de oxigênio drenando e reabastecendo na superfície.
- Regras de surfacing (cheia / parcial / vazia).
- Colisões (torpedo↔inimigo, player↔inimigo, player↔diver).
- Score + vidas + game over + HUD.

### Pós-MVP
- Patrol sub e a mecânica de refil congelado perigoso.
- Escalada de dificuldade por wave (velocidade/valores).
- Bônus de oxigênio no surface.
- Vidas extras a cada 10k.
- SFX nos eventos (tiro, explosão, resgate, alarme de oxigênio baixo).
- (Futuro distante) suporte a GBC / high score em SRAM.

---

## 5. Épicos → Milestones → Tasks

Estrutura:
```
Epic → Milestone (entregável testável) → Task (unidade implementável) → Acceptance Criteria
```

### EPIC A — Fundação técnica
**Milestone A1: "It builds and boots"**
- Task A1.1: Esqueleto do projeto + Makefile compilando um `.gb` que boota tela preta. *AC: `make` gera ROM; roda no SameBoy sem travar.*
- Task A1.2: Game loop base com sincronização de frame (`wait_vbl_done`) e leitura de input. *AC: um sprite se move com o D-Pad a 60 fps estável.*
- Task A1.3: HUD placeholder (texto de score e barra de O₂ fake). *AC: HUD renderiza sem piscar.*

### EPIC B — O submarino do player
**Milestone B1: "I can pilot the sub"**
- Task B1.1: Sprite do sub (entidade 16×8, 2 sprites 8×8), movimento livre nas 4 direções + diagonais, clamp nas bordas. *AC: sub não sai da tela; movimento suave.*
- Task B1.2: Orientação esquerda/direita conforme última direção horizontal. *AC: sprite espelha corretamente.*
- Task B1.3: Disparo de torpedo (1 na tela), saindo na direção que o sub aponta. *AC: torpedo some ao sair da tela; respeita limite de 1.*

### EPIC C — Inimigos e divers
**Milestone C1: "The ocean is alive"**
- Task C1.1: Spawner por fileiras (rows), entrando de ambos os lados, respeitando orçamento de sprites/scanline. *AC: nunca estoura 10 sprites/linha em teste de stress.*
- Task C1.2: Shark com movimento horizontal + oscilação senoidal vertical. *AC: padrão ondulado visível e previsível.*
- Task C1.3: Enemy sub com movimento horizontal + disparo de torpedo contra o player. *AC: torpedo inimigo atravessa a tela e mata o player no contato.*
- Task C1.4: Diver atravessando a tela; resgatável por contato. *AC: contador sobe; para em 6.*

### EPIC D — Oxigênio e surfacing
**Milestone D1: "Breathe or die"**
- Task D1.1: Barra de O₂ drenando por tempo; alarme visual em nível baixo. *AC: drena a taxa constante; HUD reflete valor real.*
- Task D1.2: Detecção de superfície + reabastecimento (player congelado durante refil). *AC: O₂ enche; player não aceita input durante refil.*
- Task D1.3: Regras de surfacing (6=ok+avança, 1–5=perde 1 diver, 0=morte). *AC: cada caso testado e correto.*
- Task D1.4: Morte por O₂ zerado. *AC: sub explode e perde vida/diver conforme regra.*

### EPIC E — Colisões, score e ciclo de vida
**Milestone E1: "It's a game"**
- Task E1.1 *(antecipada e feita no EPIC C, pois os ACs de C1.3/C1.4 dependem dela)*: Sistema de colisão AABB central (torpedo↔inimigo, player↔inimigo, player↔diver, torpedo-inimigo↔player). *AC: colisões consistentes; sem falsos positivos.*
- Task E1.2: Scoring (inimigos 20→90, divers 50→1000, escalonando por wave). *AC: valores batem com a tabela.*
- Task E1.3: Vidas, reserva, extra life a cada 10k, game over. *AC: fluxo completo de morte→respawn→game over.*
- Task E1.4: Avanço de wave (velocidade/valores sobem). *AC: dificuldade perceptível a cada wave.*

### EPIC F — Polish
**Milestone F1: "Juice"**
- Task F1.1: Patrol sub + perigo no refil. *AC: aparece na wave certa; mata no refil.*
- Task F1.2: Bônus de oxigênio no surface cheio. *AC: pontos extras proporcionais ao O₂.*
- Task F1.3: SFX (tiro, explosão, resgate, alarme O₂). *AC: cada evento tem som; sem travar o loop.*
- Task F1.4: Telas de título e game over. *AC: navegáveis com START.*

---

## 6. Arquitetura de código proposta (enxuta)

Seguindo o princípio "poucos arquivos coesos". Proposta inicial — **a discutir antes de implementar**:

```
seaquest/
├── Makefile
├── src/
│   ├── main.c          # entry point, input, game loop, state machine (title/play/gameover)
│   ├── game.c / game.h # game state, entities, update/render de TODAS as entidades,
│   │                   #   spawner, oxigênio, surfacing, score, waves, patrol, HUD
│   ├── collision.c/.h  # AABB + checagens (único lugar de colisão)
│   └── sound.c / .h    # efeitos sonoros (escritas diretas no APU)
├── tests/               # suíte de testes (PyBoy + pytest), ver tests/README.md
├── requirements.txt     # dependências Python dos testes (versões fixas)
├── res/                 # PNGs-fonte da pixelart (entrada do png2asset; versionados)
└── build/               # gerado pelo make (não versionado)
    └── assets/          # .c/.h gerados pelo png2asset a partir de res/*.png
```

**Pipeline de assets (png2asset):**
- Os `.c/.h` de assets **não são versionados**: o `Makefile` roda o `png2asset` (que vem no GBDK) sobre cada PNG de `res/` e gera em `build/assets/`. Para adicionar um asset, inclua o PNG na lista `SPRITE_PNGS` ou `TILESET_PNGS` do `Makefile`.
- **Sprites** (`SPRITE_PNGS`): convertidos só como tiles (`-spr8x8 -sprite_no_optimize -tiles_only`), da esquerda para a direita: uma entidade 16×8 vira exatamente 2 tiles (esquerdo, direito) e o torpedo 1 tile. `-sprite_no_optimize` impede que tiles repetidos/vazios sejam removidos, então a ordem é garantida (há `_Static_assert` no código conferindo a contagem). O render escreve direto na shadow OAM — a rotina genérica de metasprites do GBDK custava caro demais. O espelhamento esquerda/direita é feito em runtime (flag `S_FLIPX` + troca da ordem dos 2 tiles), então desenhamos só a versão virada para a direita.
- **Tilesets** (`TILESET_PNGS`): faixa horizontal de tiles 8×8 exportada sem deduplicação; a ordem dos tiles da esquerda para a direita **é** o índice na VRAM e precisa bater com o `enum` correspondente em `game.c`.
- **Formato dos PNGs:** indexado com 4 cores na ordem branco, cinza claro, cinza escuro, preto (índices 0–3). Em sprites, o índice 0 é transparente. Tiles de HUD usam cinza claro (índice 1) como fundo, que é a cor das faixas de HUD.

**Notas de design:**
- `game.c` é propositalmente o "cérebro" gordo e central. Entidades (player, sharks, subs, divers, torpedos) vivem em **arrays de structs** com um campo `active`. Nada de alocação dinâmica.
- Se `game.c` crescer demais a ponto de atrapalhar, aí sim consideramos separar (p. ex. `entities.c`), mas **só quando doer**, não preventivamente.
- `input.c` é tão fino que pode começar dentro de `main.c`; separa se crescer.
- Assets são gerados — não escrevemos tile data à mão.

**Padrões a seguir:**
- Entidades: `struct` com `x, y, active, type/variant` e campos específicos. Arrays de tamanho fixo (orçamento conhecido).
- **Posições fracionárias (ponto fixo 12.4 com sinal):** posições que se movem são do tipo `Fixed` (`int16_t`): 12 bits de pixel + 4 bits de fração (1/16 de pixel), faixa de ±2048 px. Velocidades também são `Fixed` por frame (ex.: `FIXED_ONE` = 16 = 1 px/frame, `8` = 0,5 px/frame, `12` = 0,75 px/frame). Isso permite acelerar inimigos em passos pequenos a cada wave sem saltos de 1 px/frame inteiro. A conversão é feita por `fixed_to_pixel()` / `pixel_to_fixed()` (`static inline`, nunca macro). Colisões AABB e render usam o pixel inteiro.
  - **Por que 12.4 com sinal e não 8.8:** inimigos nascem **fora da tela** (x negativo à esquerda, x ≥ 160 à direita, com o grupo enfileirado atrás). 8.8 sem sinal não representa x negativo, e 8.8 com sinal só cobre ±128 px, menos que a largura da tela. 1/16 px de precisão é mais que suficiente para a escalada de velocidade; o custo de `>> 4` é desprezível.
- Estado global do jogo numa única `struct GameState` passada por ponteiro (sem globais soltas espalhadas).
- Constantes de tuning (velocidades, valores de score, taxa de O₂) agrupadas como `const`/`enum` no topo de `game.h`, bem comentadas — fáceis de ajustar no playtest.
- Loop: `input → update → vsync() → render` (`vsync()` é o nome atual de `wait_vbl_done()` no GBDK-2020). O render vem **logo após** o `vsync()` para que as escritas de HUD na VRAM caiam no VBlank (fora dele, cada byte espera a VRAM liberar); os sprites vão para a shadow OAM, copiada para o hardware no VBlank seguinte. Sem lógica no meio do render.
- **Score em dígitos decimais** (`score_digits[6]`), não `uint32_t`: extrair 6 dígitos de um `uint32_t` exige divisões de 32 bits em software, que custavam mais que um frame inteiro.

### 6.1. Desempenho com SDCC no SM83 (medido no EPIC C)

O orçamento é de **154 scanlines por frame** (70.224 ciclos de clock; ~17.500 ciclos de máquina). Em C com SDCC, cada comando simples custa da ordem de 40 ciclos de máquina, porque a CPU tem poucos registradores e as variáveis vivem na pilha. Regras que saíram das medições (e que devemos seguir daqui em diante):

- **`tests/test_performance.py` falha se o orçamento estourar**, e `make test` imprime o pior frame de cada cenário. Para investigar, `make profile` mostra o custo por função e por linha de C.
- **Medir sempre o pior caso**, não a média. Meta: pior frame real ≤ ~85–90% do orçamento, deixando folga para os próximos epics. Estado após a otimização: pior frame observado em jogo ~138/154 scanlines; cena vazia ~55.
- **Não percorrer slots vazios.** Loops sobre arrays com flag `active` custavam caro mesmo sem nada ativo; preferir máscaras de bits que terminam após o último elemento vivo (ver `Lane.alive_mask`).
- **Evitar comparações de 16 bits com sinal** em caminhos quentes: a CPU não tem flags de sinal e cada uma vira ~20 instruções. Usar `is_in_range()` (uma comparação sem sinal) ou testes de sinal (`x < 0`).
- **Colisões em 8 bits** (espaço de colisão deslocado +16 px) e com *broad phase* por fileira: um teste de y descarta fileiras inteiras.
- **Copiar campos de struct para variáveis locais** antes de loops quentes, em vez de reler `ptr->campo` a cada iteração.
- **Nada de divisão/módulo de 16/32 bits por frame.** Só em eventos raros (ex.: spawn).
- **Escritas de VRAM (HUD) no VBlank** e só quando o valor muda.
- **Flags do SDCC:** `--max-allocs-per-node50000 --opt-code-speed` (~8% de ganho, custa só tempo de compilação).
- Quando ainda assim não couber: **limitar quantidade** (tetos de entidades) antes de sacrificar clareza do código.

---

## 7. Toolchain (ambiente do Carlos)

- **WSL2 (Ubuntu) no desktop Windows**, editando via VSCode Remote-WSL.
- **GBDK-2020** (binário Linux x64) + **SDCC** (vem junto).
- **png2asset** (vem no GBDK-2020, em `gbdk/bin/`) para converter PNG → tiles de sprites/tilesets. Chamado automaticamente pelo `Makefile` (ver seção 6).
- **make** para build.
- **Python 3** com `venv`, para a suíte de testes: `make test-env` cria o ambiente em `.env/` com as dependências de `requirements.txt` (PyBoy, Pillow, pytest).
- **Emulador:** mgba-qt (`make run`, ver README.md) para rodar/debugar. No mGBA, o botão A do Game Boy fica na tecla **X** e o B na **Z** (a tecla A do teclado é o L do GBA); BGB/Emulicious nativos no Windows como alternativa de debug.
- Arquivos do projeto no **filesystem nativo do WSL** (`~/...`), não em `/mnt/c/`, para I/O rápido.

---

## 8. Como começamos

Sugestão de primeira conversa com o Claude Code:
1. Confirmar/ajustar a **estrutura de arquivos** da seção 6.
2. Fechar as **constantes de tuning** iniciais (tamanho de sprite do sub, velocidades, taxa de O₂, orçamento de entidades).
3. Implementar **EPIC A completo** (projeto que builda, boota e move um sprite com input a 60fps).
4. Só então seguir para o player sub (EPIC B).

Fonte da descrição de gameplay: descrição fornecida pelo Carlos + verificação em Wikipedia (Seaquest, 1983, Steve Cartwright/Activision), pixelatedarcade.com e tasvideos.org para comportamento de inimigos e mecânica de refil.
