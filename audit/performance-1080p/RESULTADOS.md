# Validação no MiSTer em 1080p — 13–14/11/2026

Atualização de 14/11: a investigação específica do Linux está em [LINUX.md](LINUX.md).
O mesmo binário otimizado respondeu a duas novas sequências de 1.000 comandos
(medianas 97,563 e 97,815 ms), sem mudança de kernel. Não há políticas CPUFreq
expostas neste boot; o A/B de governador ficou indisponível. Esses números
não substituem a comparação histórica abaixo nem representam novo ganho de código.

Esta etapa complementa o relatório histórico da etapa 1 e a análise `ANALISE.md`.
A resolução foi mantida em **1920×1080**, com tema Carbon, duas threads,
**STRIP=8 e PLD=256**. Não foram reduzidos resolução, filtro, efeitos ou duração
das transições para obter os ganhos.

## Método e origem

O executável originalmente instalado tinha SHA-256
`253a15744237071c9bc136d94666957cc4567fbbe8ebf349ce1a10cd86daf404`, idêntico ao pacote 18.
O ZIP recebido foi preservado. A comparação com o código local e a reconstrução
do patch 29 estão em `comparacao.json` e `comparacao-zip-instalada.diff`.
Não se afirma ter reconstruído bit a bit o executável original.

A compilação final é Release/O3, estática, ARMv7 hard-float, Cortex-A9/NEON,
com Zig 0.15.2. SHA-256 do executável distribuído:
`d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64`.
Cada execução usou configurações e HOME isolados, mantendo recursos e catálogo
do aparelho. Nenhum jogo existente foi lançado, removido ou alterado pelos benchmarks.

O controle reconstruído conserva os caminhos anteriores do renderer com
`ES_SOFT_OPTIMIZATIONS=0`. Um controle adicional do executável realmente instalado
teve mediana de resposta interna de 112 ms em 32 comandos. A reconstrução com
otimizações desligadas ficou em 109 ms nos testes de 48 comandos; a versão
otimizada curta ficou em aproximadamente 92 ms. Esses testes curtos não bastam
para estabelecer p99.

A comparação sustentada utiliza **1.000 comandos por execução**, alternando
quatro movimentos à direita e quatro à esquerda, intervalados, sem benchmark
de redesenho forçado, replay de slow dumps ou profiler de sinais. O tempo contado
vai do timestamp de entrada na fila SDL ao término do primeiro redesenho que a
responde. Também foi preservada a métrica antiga, a partir do despacho no loop.
Isso mede software; **não mede latência física controle–TV** nem o fim da animação.
As teclas vêm de SDL_PushEvent, não de um controle físico.

O tracer grava em RAM, tem limites de 8.192 quadros, 65.536 amostras de comandos
e 8.192 respostas de entrada e só escreve os CSVs no encerramento. Os 1.000 inputs
são registrados independentemente do limite de quadros. Memória é amostrada a 1 Hz.
O perfil por etapa cobre o início do teste até o limite do buffer, não todos os
quadros dos doze minutos. p99 usa nearest rank, sem interpolação; é descritivo de
uma sequência por versão, não um intervalo de confiança ou garantia de pior caso.

<!-- MEASUREMENTS BEGIN -->
## Comparação sustentada

| Medida (ms) | Controle | Otimizada | Redução |
|---|---:|---:|---:|
| Fila SDL → redesenho, mediana | 121.644 | 102.530 | 15.7% |
| Fila SDL → redesenho, p95 | 135.504 | 118.809 | 12.3% |
| Fila SDL → redesenho, p99 | 144.798 | 132.384 | 8.6% |
| Fila SDL → redesenho, máximo observado | 240.934 | 195.961 | 18.7% |
| Despacho → redesenho, mediana | 107.052 | 90.695 | 15.3% |
| Despacho → redesenho, p99 | 130.138 | 119.625 | 8.1% |

Cada versão respondeu exatamente 1.000 comandos. O controle durou 728.8 s e a otimizada 767.0 s, incluindo inicialização e encerramento. A duração total também inclui esperas artificiais do script; não é uma medida de throughput.

| Etapa do redesenho completo, mediana (ms) | Controle | Otimizada |
|---|---:|---:|
| Preparação da UI | 3.532 | 2.678 |
| Regiões sujas | 0.104 | 0.104 |
| Execução incluindo cópia | 98.969 | 90.280 |
| Desenho do worker mais lento | 89.172 | 81.909 |
| Cópia do worker mais lento | 11.426 | 9.695 |
| Espera do limitador | 16.752 | 0.003 |

As distribuições de quadros diferem porque o limitador corrigido permite mais passos de animação e o culling permite atualizações parciais. A comparação principal é a resposta aos mesmos inputs. **Não se atingiu 60 FPS nos redesenhos completos do Carbon em 1080p.**

Memória residente máxima durante o teste instrumentado: 165.23 MiB no controle e 165.32 MiB na otimizada. Depois de preencher o buffer de tracing e antes do encerramento, as medianas de janelas de 100 s foram 165.309 e 165.320 MiB na otimizada. Não houve crescimento relevante nessa janela; isso não prova ausência de vazamentos em todas as telas. O tracing reserva e ocupa memória progressivamente; os números não são o consumo mínimo da execução normal.

Na validação separada de qualidade, **0 de 91 quadros verificados divergiram** entre renderização parcial e referência completa. Carrossel, lista NES, mudança de seleção e menu foram capturados em 1920×1080 e inspecionados visualmente. Esse modo redesenha a referência e salva PNGs; seus tempos não são usados como benchmark.

Uma thread teve mediana interna de 134.652 ms em 24 comandos, contra 90.695 ms com duas threads no teste longo. As amostras têm tamanhos diferentes, mas não apareceu razão para trocar o padrão de duas threads.

Inicialização de processo novo com cache do sistema já aquecido, uma execução de cada:

| Versão | Marca ready (s) | Primeiro comando respondido (s) |
|---|---:|---:|
| startup-control-final | 8.165 | 9.733 |
| startup-fast-final | 8.258 | 9.664 |

O script aguarda 9 s antes da tecla; o primeiro comando confirma funcionamento nesse instante e é um limite superior, não uma busca do primeiro milissegundo interativo. Os valores excluem a espera inicial do wrapper. Não houve reboot/limpeza global de cache para medir cold boot e não se conclui ganho estatístico de startup com uma amostra.

<!-- MEASUREMENTS END -->

## O que explicava o custo

No controle com amostragem detalhada, os redesenhos completos da navegação tinham
medianas aproximadas de 3,51 ms preparando a UI, 0,10 ms calculando regiões sujas,
100,08 ms executando o renderer (incluindo cópia), 90,15 ms de desenho no worker
mais lento e 11,14 ms de cópia. Essas medianas são distribuições independentes e
não devem ser somadas como se viessem de um mesmo quadro.

O limitador ainda acrescentava aproximadamente **16,75 ms de espera depois de
quadros já atrasados**. Acordar os workers custava cerca de 0,11 ms. Nas amostras
detalhadas de navegação houve 3.679 consultas de cache, todas hits, sem geração
de linhas ou alocações do cache; não apareceu evidência de que o mutex do cache
fosse o gargalo dominante nessa carga aquecida.

O custo principal está na rasterização/composição e movimentação de pixels.
Havia comandos de texto com centenas de vértices cuja opacidade inteira era
zero. Mesmo invisíveis, percorriam faixas, vértices e preparação dos glifos.
Fundos e overlays de tela inteira continuam custosos em 1080p.

Os 150–160 ms dos logs antigos são observações de quadros específicos, incluindo
transições e aquecimento; não constituem uma mediana universal. Os slow dumps
antigos redesenham comandos em série para diagnóstico e não equivalem ao tempo
paralelo original. A comparação de ganho usa execuções controladas, não subtrai
92 ms diretamente de 160 ms para anunciar uma melhoria artificial.

## Correções aplicadas

- Descartar um comando somente se todos os vértices têm alfa zero, blend normal
  SRC_ALPHA/ONE_MINUS_SRC_ALPHA e não há escrita de stencil. Quadros contendo
  apenas comandos descartados ainda limpam os pixels do quadro anterior.
- Rejeitar quads fora da faixa antes de preparar cores e interpolação dos glifos.
- Manter o limite de FPS sem impor uma espera extra de um quadro quando o desenho
  já consumiu o orçamento. Transições continuam guiadas pelo tempo real.
- Especializar NEON para modulação de alfa com RGB branco, conservando a mesma
  aritmética de alfa e blend e o prefetch. A comparação de pixels valida essa
  especialização; os microbenchmarks variáveis não sustentam um ganho isolado preciso.
- Desligar BENCH, PROFILE e SLOW_DUMPS na configuração de uso normal. Os dumps
  permanecem disponíveis para diagnóstico explícito.
- Corrigir o helper para reconhecer o framebuffer já ativo quando reaberto por
  SSH. A fotografia mostrava o login do Linux e o frontend não era iniciado pelo
  helper anterior. O novo helper confirmou abertura em 1080p em 82,7 ms; auto/2
  seguido de auto confirmou retorno a 1920×1080. Esses números medem o helper,
  não a carga do catálogo ou toda a inicialização.

O diagnóstico do console combina a fotografia, a confirmação de fb_cmd no
aparelho e a inspeção do [Main_MiSTer oficial](https://github.com/MiSTer-devel/Main_MiSTer/blob/master/menu.cpp).
Não se afirma que esse arquivo corresponda bit a bit ao Main instalado.

## Integridade e pendências do relatório anterior

| Item | Resultado nesta etapa |
|---|---|
| P01 HTTP | Servidor permanece vivo até o join. Passaram 64 ciclos imediatos de início/fim, handler ativo além do timeout interno de stop e exceção no início, sob ASan/UBSan/LSan. |
| P02 ThreadPool | Filas/flags sincronizadas, espera por condition_variable, reuso, cancelamento, fallback para zero cores e exceções. Passaram 32 reusos, 4 produtores/4.000 jobs, trabalhos aninhados e exclusões. Não foi executado ThreadSanitizer. |
| P03 copyFile | Buffer de 64 KiB, temporário exclusivo no diretório do destino, validação de leitura/escrita/flush/close e publicação só ao concluir. Passaram origem ausente, mesmo arquivo/hardlink, symlink, permissões e falha real de escrita por RLIMIT_FSIZE, preservando o destino anterior. Windows não foi executado. |
| P04 instaladores | Tema Git passa a ser extraído/validado em staging, preservando o antigo e fazendo rollback em falha de publicação. Passaram CRC inválido, traversal e EXDEV injetado na publicação. Os instaladores específicos de Win32 continuam fora da validação MiSTer e não estão declarados corrigidos integralmente. |
| P05 busca | Jaro-Winkler deixa de zerar dois arrays fixos de 65.000 inteiros na pilha por par; usa armazenamento dimensionado e limites explícitos. Passaram 1.240 resultados idênticos à fórmula antiga em entradas válidas e casos de até 100.000 bytes. A divisão inteira original das transposições foi preservada para manter a ordenação. |
| P06 build | pugixml recuperado na revisão 61c944895e071872370219cadade8423dc6e04dc; RapidJSON e controles fixados em commits; ALSA passa a instalar em DESTDIR para não escrever configurações no host; build ARM completo executado. Fixar revisões não significa build bit a bit reproduzível nem auditoria de dependências/CVEs. |
| P07 cobertura | Permanece aberta a revisão integral das demais áreas e plataformas. O inventário original da etapa 1 é histórico; não representa uma nova revisão manual de todos os 1.008 arquivos. |

As oito suítes Linux passam com ASan/UBSan/LSan: HTTP/ZIP/arquivos (17 cenários),
Settings, backend, servidor, pool, cópia, similaridade e temas. As 16 cenas
diferenciais do rasterizador produzem os mesmos hashes de pixels no ARM/NEON e
no teste nativo. Isso não equivale a testar todas as combinações de tema/controle.
Não houve teste físico de áudio, hotplug, lançamento de MGL ou retorno de core.

No ARM, um microbenchmark sintético de busca com 2.000 pares passou de 717,845 ms
para 17,762 ms. É uma medida desse componente e dessas strings; não significa
que a interface inteira ficou quarenta vezes mais rápida.

A build ARM continua sem HTTPS, vídeos de prévia e shaders, conforme as limitações
prévias do port. Não foram desativados recursos adicionais para obter desempenho.
O salvamento por renomeação protege contra as falhas de operação testadas, mas
não fornece garantia transacional em perda de energia. O código de extração não
oferece isolamento contra outro processo privilegiado alterando links durante a operação.

## Reproduzir as medições

`ANALISE.md` descreve os pontos de instrumentação. Num lançamento controlado,
use `ES_SOFT_TRACE=/caminho/run.csv` e opcionalmente `ES_SOFT_TRACE_DETAIL=16`.
Mantenha ES_SOFT_FPS=60, ES_SOFT_STRIP=8, ES_SOFT_PLD=256 e os mesmos recursos.
Use ES_SOFT_OPTIMIZATIONS=0/1 para comparar os caminhos. Não execute coleta,
compactação de logs ou outro benchmark no ARM enquanto mede navegação.

Os scripts da bancada e logs desta etapa ficam em `evidencias-etapa2`; as chaves
SSH não fazem parte dos artefatos. A atualização contém manifesto SHA-256,
instalador com backup e preserva jogos, metadados, controles e temas instalados.
Para voltar à versão anterior, encerre a interface pela API local, restaure os
cinco arquivos da pasta de backup registrada na implantação e reinicie o launcher.

## Implantação conferida em 14/11/2026

A atualização foi instalada e reiniciada pelo `es-mister.sh` normal. Foi conferido
um único frontend ativo, executável e helper com os hashes do manifesto, framebuffer
1920×1080, STRIP=8, PLD=256, FPS=60 e resposta da API local de sistemas. Tracing,
BENCH, PROFILE, SLOW_DUMPS, scripts de teste e verificação dupla de pixels estavam
desligados no processo de uso normal. O log marcou ready em 9,537 s a partir do
início do processo nessa inicialização após reboot; esse número exclui o wrapper.

Backup dos cinco arquivos anteriores:
`/media/fat/emulationstation/backups/codex-1080p-19691231-210331-561759`.
O nome reflete o relógio sem sincronização do aparelho após reboot; a implantação
foi observada em 14/11/2026 no computador. O helper original anterior à correção
também está preservado em `codex-performance-20261113/backup/es-mister-helper`.
O registro completo é `evidencias-etapa2/deployment-verification.json`.
