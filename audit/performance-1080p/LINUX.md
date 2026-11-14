# Linux do MiSTer e latência — 14/11/2026

## Conclusão e escopo

Investigação no aparelho 192.168.15.14, preservando o binário instalado
`d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64`, catálogo,
Carbon, saída nativa 1920×1080, duas threads de desenho, STRIP=8, PLD=256,
ES_SOFT_FPS=60 e otimizações já existentes. Não houve overclock, carga de módulo,
troca de governador, sysctl, alteração de swap, drop_caches ou ajuste permanente.

**Não existe política CPUFreq registrada neste boot. O A/B original/performance
é indisponível; o ganho dessa troca é não medido.** O diretório
`/sys/devices/system/cpu/cpufreq` existe vazio; não existem `policy*` nem
`cpu[0-9]*/cpufreq`. Isso foi verificado também sob carga pela amostragem a 1 Hz.
Não se atribui latência a uma leitura de frequência em repouso.

## Inventário observado

| Item | Aparelho |
|---|---|
| Kernel | Linux **6.18.38-MiSTer**, `#2 SMP Sat Sep 12 20:19:51 CST 2026`, armv7l |
| CPUs online | 0–1; Cortex-A9, ARMv7, NEON |
| CPUFreq compilado | `CONFIG_CPU_FREQ=y`; padrão compilado `performance` |
| Governadores compilados | performance, powersave, userspace, ondemand, conservative, schedutil |
| Políticas expostas | Nenhuma |
| Driver CPUFreq ativo | Não exposto/nenhum registrado pela interface consultada |
| Governador CPUFreq ativo | Não exposto; o padrão compilado não comprova um governador ativo |
| Frequências permitidas, mínima/máxima e atual | Não expostas; frequência física não medida |
| Módulo disponível em disco | `socfpga-cpufreq.ko.xz`, nome `socfpga_cpufreq`, descarregado |
| Descrição do módulo | `MiSTer Cyclone V CPUFreq with explicit overclock opt-in` |
| CPUIdle | driver `none`, governador informado `ladder`, nenhum `state*` exposto |
| Preempção/tick | `CONFIG_PREEMPT_NONE=y`, `CONFIG_HZ_1000=y`, tick periódico |
| RAM visível | 502.864 KiB = 491,08 MiB |
| Swap | 0 KiB, `/proc/swaps` sem dispositivos; pswpin=pswpout=0 |
| PSI | Não compilado (`CONFIG_PSI` desativado); `/proc/pressure/*` indisponível |
| Memória disponível no inventário durante navegação | 231.688 KiB = 226,26 MiB; faixas por cenário abaixo |

`BogoMIPS=200` não foi usado como medida do clock. O diretório de clocks em
debugfs não estava disponível; não se montou debugfs nem se leram registradores
PLL por acesso físico. Não se carregou um módulo para fabricar uma política
ausente na configuração original.

CPUFreq precisa de um driver registrado para criar as políticas e seus controles.
CPUIdle é outro subsistema; o nome `ladder` não significa que seja o governador
de frequência. Referências primárias:
[CPU performance scaling](https://docs.kernel.org/admin-guide/pm/cpufreq.html) e
[CPU idle](https://docs.kernel.org/admin-guide/pm/cpuidle.html).

## Método de medição

- Navegação: sequência histórica de **1.000 comandos**, quatro Right/quatro Left,
  key-down 60 ms, espera de 150 ms após key-up e espera de 450 ms do roteiro.
  Há 14 s iniciais artificiais; eles são excluídos da navegação e não medem startup.
  Entradas por SDL_PushEvent, como na referência histórica.
- Repouso: dispositivo uinput persistente criado **antes** da inicialização SDL;
  cada toque alterna Right/Left, dura 60 ms e vem após **15 s fixos** de repouso.
  Há 3 s separados para terminar a transição anterior. Além da métrica SDL,
  registra-se submissão uinput → fim da cópia do primeiro redesenho. Isso inclui
  entrada Linux/evdev/SDL, mas não rádio/USB de um controle físico nem scanout/TV.
- HOME/configuração isolados; mesmo catálogo e temas do aparelho. Nenhum jogo
  existente, save ou metadado é alterado. O launcher normal é restaurado ao final.
- `ES_SOFT_TRACE_DETAIL=0`; double-check, PNG, profiler de sinais, slow dumps e
  redesenho forçado desligados. Tracer em RAM, limitado a 8.192 quadros e 8.192
  respostas, escrito ao sair. Respostas cobrem os 1.000 comandos; tempos por
  etapa cobrem somente a janela até o limite de quadros, identificada no JSON.
- Monitor em Python a 1 Hz: MemAvailable/RSS, major faults do processo,
  pswpin/out, reclaim direto/kswapd, allocstall, refaults, CPU e políticas.
  Guarda em RAM e grava JSON ao fim. A resolução de 1 s não prova causalidade
  para uma pausa de 100 ms; por isso também se examinam janelas dos dez inputs
  mais lentos e tempo de CPU por worker já disponível no trace.
- Percentis nearest-rank. Amostras pequenas de repouso são descritivas; p99
  coincide com o máximo se houver menos de 100 respostas. Não são garantias.
- Ordem: navegação original 1 → piloto uinput → repouso original → navegação
  original 2. É repetição da configuração original, **não um A/B de governador**.
  A primeira navegação incluiu uma coleta de inventário/hash durante a execução;
  a repetição 2 é a confirmação sem essa leitura extra. Compactação/coleta dos
  artefatos é feita apenas com a aplicação encerrada.
- Houve falhas nas consultas de status SSH do processo de acompanhamento no
  host. Os roteiros e monitores continuaram autonomamente no MiSTer; a coleta
  foi retomada por novas conexões, conferindo 16/16 e 1.000/1.000 respostas.
  O acompanhamento host antigo foi encerrado antes da coleta manual final.
  Os logs de transporte e da conclusão manual foram preservados.
- A data deste relatório vem do host. O relógio civil do aparelho estava sem
  sincronização e não foi ajustado; todos os intervalos de latência usam relógio
  monotônico, inclusive o pareamento uinput/renderer.

<!-- KERNEL RESULTS BEGIN -->
### Resposta ao primeiro redesenho (ms)

Todas as linhas novas usam o **mesmo binário original otimizado**, sem ajuste de
kernel. As diferenças para o histórico não são ganho de código ou governador.

| Cenário | Respostas | Mediana | p95 | p99 | Máximo |
|---|---:|---:|---:|---:|---:|
| Referência histórica otimizada | 1.000 | 102,530 | 118,809 | 132,384 | 195,961 |
| Navegação original 1 | 1.000 | 97,563 | 112,076 | 151,822 | 176,928 |
| Primeiro comando após repouso de 15 s, fila SDL | 16 | 23,669 | 27,254 | 27,254 | 27,254 |
| Mesmo repouso, desde submissão uinput | 16 | 24,386 | 28,564 | 28,564 | 28,564 |
| Navegação original 2 | 1.000 | 97,815 | 120,360 | 146,695 | 185,965 |

O repouso nominal de 15 s mediu 15,014–15,015 s. Os 16 toques foram respondidos.
No trecho do trace que permite relacionar input e quadro, **10/10 primeiras
respostas do repouso foram parciais**, com execução mediana de 16,515 ms.
Na navegação 1, **377/377 respostas relacionadas foram quadros completos**;
na repetição 2, **381/381**.
Os outros inputs estão no CSV de respostas, mas fora da janela limitada de
quadros. O cenário de repouso alterna dois sistemas adjacentes; a sequência
histórica visita quatro posições para cada lado. Não se compara esses números
como se diferissem apenas pelo repouso ou por um governador.

Na repetição 2, fila antes do despacho teve mediana de 16 ms (p99 17 ms), e
despacho → redesenho teve mediana de 82,304 ms. A injeção histórica por
`Script::frame()` ocorre dentro de swapBuffers antes da espera final do
limitador; a fila inclui o tempo até a próxima iteração. Esse campo não é
uma medição isolada de escalonamento do Linux. No repouso via uinput, a fila
mediu mediana 0 ms e p99 1 ms; a medição externa também inclui a etapa evdev/SDL.

Depois da primeira resposta parcial, os quadros completos da animação continuam
caros. O tempo até terminar a transição não foi medido nesta investigação do
kernel; as durações configuradas não foram alteradas.

### Renderer na janela registrada

| Medida (ms) | Navegação 1, mediana / p95 / p99 | Navegação 2, mediana / p95 / p99 | Repouso, mediana / p95 / p99 |
|---|---:|---:|---:|
| Execução completa com cópia | 81,575 / 97,453 / 116,876 | 81,843 / 100,565 / 137,552 | 83,355 / 103,288 / 164,040 |
| Desenho do worker mais lento, parede | 73,400 / 90,351 / 109,213 | 73,906 / 93,153 / 126,744 | 75,417 / 96,716 / 156,575 |
| CPU do mesmo worker | 70,632 / 82,704 / 89,060 | 70,835 / 83,484 / 88,922 | 70,535 / 79,465 / 152,613 |
| Fora da CPU, por quadro/worker crítico | 1,696 / 21,487 / 39,467 | 1,918 / 24,323 / 53,132 | 2,903 / 25,033 / 30,679 |
| Maior cópia entre workers | 9,285 / 12,450 / 16,515 | 9,163 / 12,464 / 16,626 | 9,895 / 12,203 / 12,693 |
| Publicação → início do worker, maior | 0,106 / 1,651 / 2,378 | 0,109 / 1,642 / 2,409 | 0,098 / 0,297 / 2,514 |

Navegação 1: 2.246 quadros completos, janela de trace com 290,946 s entre o primeiro
e o último registro; navegação 2: 2.260 quadros completos, janela de 293,591 s.
Repouso: 59 quadros completos dentro da janela; p99 com essa amostra é o máximo.
Tempos de parede/CPU sugerem bloqueios ou desescalonamento relevantes em parte
da cauda, sem identificar o responsável. **O custo típico continua no desenho
efetivamente executado e na cópia**, não na espera para despertar os workers.

### Memória e paginação

| Durante os inputs | Navegação 1 | Navegação 2 | Repouso 15 s |
|---|---:|---:|---:|
| MemAvailable mínimo / mediana (MiB) | 218,930 / 223,969 | 217,207 / 222,941 | 247,109 / 250,562 |
| RSS máximo (MiB), com tracing | 165,629 | 165,633 | 135,195 |
| Swap-in / swap-out (páginas) | 0 / 0 | 0 / 0 | 0 / 0 |
| Major faults do frontend | 0 | 0 | 0 |
| Bytes lidos pelo frontend no armazenamento | 0 | 0 | 0 |
| Major faults globais | 15 | 1 | 0 |
| Eventos allocstall globais | 6 | 0 | 0 |
| Páginas recuperadas diretamente | 262 | 0 | 0 |
| Páginas recuperadas por kswapd | 6.769 | 978 | 0 |

São deltas na janela dos inputs, excluindo a carga inicial do processo. No
repouso, todos os deltas registrados de reclaim/refault/paginação foram zero.
No teste 1 há atividade global limitada: 2 das 10 janelas mais lentas apresentam
reclaim direto/allocstall em uma margem de ±1 s; as outras 8 não apresentam esses
eventos, incluindo os três maiores atrasos. A margem pode incluir atividade
posterior à resposta e não demonstra causalidade. A coleta extra de inventário
também é um fator de interferência nessa primeira rodada. Não há base para
atribuir a latência típica à falta de RAM ou justificar um sysctl de memória.

Na repetição 2, nenhuma das dez janelas mais lentas teve swap, major faults ou
reclaim (direto ou kswapd). A cauda persistiu com zero allocstall/reclaim direto
em toda a navegação. Isso enfraquece a hipótese de paginação como explicação
principal; não exclui interrupções, escalonamento ou locks da aplicação.

Uma inspeção somente leitura de zoneinfo durante o repouso encontrou zona Normal
com 10.917 páginas livres e watermark high de 1.062; min_free_kbytes=2833,
watermark_scale_factor=10 e lowmem_reserve_ratio=32/0 permaneceram inalterados.
Essa fotografia não substitui os deltas por janela nem prova o estado durante
uma pausa anterior. A CPU do processo no intervalo de repouso teve mediana de
11,607% de um núcleo; é uso com tracing ativo e inclui todo o intervalo de 15 s.

**A/B de governador: indisponível. Ganho ao sair do repouso ou durante navegação:
não medido.** Nenhum ajuste de kernel foi aceito ou incorporado à configuração.

Verificação final no aparelho: um frontend normal pronto, API de sistemas HTTP
200, HOME original, mesmo binário/helper, mesmos hashes de settings/input/sistemas,
framebuffer 1920×1080×32, FPS60/STRIP8/PLD256, diagnóstico pesado desligado,
teclado de teste removido, módulo CPUFreq ainda descarregado e mesmo boot.
`swappiness=60` e `overcommit_memory=0` foram apenas lidos e permaneceram iguais.
<!-- KERNEL RESULTS END -->

## Interpretação dos tempos

`execute_ns` inclui rasterização e cópia. Para cada quadro, o worker de maior
tempo de desenho é identificado e comparam-se **o tempo de parede e o de CPU
desse mesmo worker**. A diferença estima tempo fora da CPU, incluindo bloqueios
e desescalonamento; não identifica qual tarefa/IRQ/lock o causou. Espera por DDR
durante execução continua contando como CPU time. Pequenas diferenças negativas
de relógio/instrumentação são limitadas a zero no resumo, mantendo os CSVs brutos.

As medianas de cada etapa não são somadas como se pertencessem ao mesmo quadro.
Tempo até primeiro redesenho também não é fim de transição, 60 FPS alcançados ou
latência física controle–TV. PREEMPT_NONE, isoladamente, não prova que o kernel
seja responsável pelos redesenhos completos de aproximadamente 90 ms históricos.

Não será ajustada memória sem pressão ou paginação associada às pausas. Contadores
acumulados antes do teste têm reclaim e major faults de cargas anteriores; os
deltas durante a navegação, e não esses totais, são a evidência relevante.

## Reproduzir

Os três utilitários estão em `tests/audit/`: `kernel_probe.py`,
`idle_keyboard.py` e `kernel_analyze.py`. Usam somente a biblioteca padrão Python.
Nenhum entra no binário ARM distribuído por `mister/build.sh es`.

Inventário somente leitura, no MiSTer:

```sh
python3 kernel_probe.py inventory --output kernel-inventory.json \
  --binary /media/fat/emulationstation/emulationstation
python3 kernel_probe.py sample --pid PID_DO_ES --interval 1 \
  --output kernel-samples.json
```

No workspace desta auditoria, PowerShell (SSH por chave já configurado):

```powershell
Set-Location -LiteralPath 'C:\work\es-mister-audit'
wsl -d Ubuntu-26.04 -- python3 ./validation/kernel_investigation.py
```

O roteiro usa nomes únicos de execução. Para repetir depois de já existirem os
artefatos, escolha novos rótulos no roteiro, preservando os anteriores. Não execute
dois frontends/roteiros simultâneos. Para resumir um diretório já coletado:

```powershell
wsl -d Ubuntu-26.04 -- python3 ./es-mister/tests/audit/kernel_analyze.py `
  ./validation/target-results/kernel-nav-original-1 `
  ./validation/target-results/kernel-idle-original-1 `
  ./validation/target-results/kernel-nav-original-2
```

Caso um futuro kernel exponha políticas ajustáveis: primeiro guardar driver,
limites, frequências e governador de **cada** política, confirmar `performance`
em `scaling_available_governors`, e comparar original/performance/performance/
original para cada cenário, usando o mesmo executável. Somente o governador muda;
não se alteram limites, clocks, renderer ou concorrência. Restaurar todos os
valores originais em `finally`/trap, inclusive após erro, e conferir por leitura.
Esse percurso não foi executado nem validado neste kernel sem políticas.
