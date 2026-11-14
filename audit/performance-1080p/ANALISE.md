# MiSTer em 1080p: diagnóstico antes de otimizar

13/11/2026. Análise do código local e dos logs existentes. Nenhum renderizador, parâmetro de vídeo, prefetch ou tamanho de faixa foi alterado. A aplicação instalada permaneceu em execução. Foram criados apenas referências de análise, diffs e este documento.

**Conclusão principal**

Os 150–160 ms são o tempo de desenho do worker mais lento, não o tempo completo entre apertar um botão e ver a resposta. Cada worker desenha sua região da tela em várias faixas de 8 linhas; esse número não é o tempo de uma única faixa de 8 linhas. Há evidência forte de custo nas camadas grandes com transparência, leituras/escritas de pixels e caminhos de imagens escalonadas. Entretanto, os logs ainda não separam o trabalho efetivo dos kernels, preparação repetida por faixa, espera de mutexes, atraso de escalonamento e sincronização. A próxima mudança deve primeiro medir esses componentes no caminho real de faixas de **8 linhas, com PLD=256**, mantendo os mesmos pixels e o mesmo tema.

**Versões comparadas e preservação do trabalho existente**

- O executável instalado tem SHA-256 `253a15744237071c9bc136d94666957cc4567fbbe8ebf349ce1a10cd86daf404`, idêntico ao binário de `es-mister-7ad64ca-18/emulationstation/emulationstation`.
- O repositório local em `D:/FPGA/Frontend/Mistercera/es-mister` está limpo no commit `fcd0450c1c5cef720ce08690c217f2aab40da620`.
- Todos os arquivos de renderização presentes no ZIP `es-mister-src-5b5c7bc.zip` coincidem com esse repositório. Entre os arquivos do ZIP, apenas `mister/LEIA-ME.txt` e `mister/package.sh` diferem do repositório local; o repositório também possui o benchmark adicionado posteriormente.
- A diferença relevante para o pacote instalado é descrita pelo patch 29, commit informado `3e0121f0eea22fcd8650d70e6b295e0a04a58bde`. Ele acrescenta prefetch e cópias NEON com prefetch e muda os padrões de 16 para 8 linhas. O patch foi aplicado com sucesso **somente a uma referência temporária de análise**. A referência resultante está em `referencia-instalada/`; o diff com o ZIP está em `comparacao-zip-instalada.diff`.
- A identidade binário/pacote está confirmada. A referência de fontes foi reconstruída a partir do repositório e do patch correspondente; não se realizou recompilação bit a bit para provar a origem de cada byte do executável.

| Item | ZIP / fontes locais anteriores ao patch 29 | Pacote 18 / configuração observada |
|---|---|---|
| Resolução de saída | Port permite framebuffer nativo | 1920×1080, 32 bpp |
| Faixas (`STRIP`, `ES_SOFT_STRIP`) | Padrão 16 linhas | **8 linhas** |
| Prefetch dos spans | Ausente | **256 bytes** à frente; destino e fonte direta |
| Cópias de pixels opacos/escalados | `memcpy` nos caminhos alterados | `copyPixels`, NEON com PLD |
| Cópia para framebuffer | NEON sem pedido adiantado da fonte | NEON com prefetch da fonte |
| Threads de desenho | Até duas por padrão | Duas nos logs |
| Redesenho parcial e cache escalonado | Já existem | Preservados; não são novidades do patch 29 |

Não faria sentido usar o ZIP sem esses ajustes como uma suposta atualização de desempenho. Eles devem compor a referência de qualquer medição posterior.

**O que os logs realmente mostram**

O hardware observado possui dois processadores ARMv7 com NEON, aproximadamente 491 MiB de RAM visível ao Linux e framebuffer 1920×1080×32. A consulta por SSH encontrou ES com nove threads e cerca de 207 MiB de RSS naquele instante. Esses dados não demonstram, por si, pressão de memória ou disputa excessiva entre threads.

O arquivo `es-mister-stats.txt` agrega várias versões e sessões ao longo do dia. Não usei as médias antigas como referência do pacote 18. Após a instalação às 19:30, os logs locais disponíveis contêm seis janelas de 600 iterações: três com algum desenho e três sem redesenho.

| Registro | Medida observada | Interpretação |
|---|---|---|
| `es-startup.log`, 45,858 s | ES 6 ms; desenho **157 ms**, 100% da tela | Exemplo de navegação em que a preparação do ES é pequena e o desenho domina. |
| `es-startup.log`, 46,282 s | ES 6 ms; desenho **156 ms**, 100% da tela | Repetição do custo de redesenho completo. |
| `es-startup.log`, 61,926 s | ES 5 ms; desenho **298 ms**, 100% da tela | Há picos além dos 150–160 ms. Falta decomposição do kernel/cache/scheduling. |
| Estatísticas 19:35:07 | 295 redesenhos; desenho médio 77,3 ms, máximo 283,0 ms; cópia média 5,47 ms | Mistura quadro completo, parcial e início da sessão. Não significa custo de um quadro completo típico. |
| Mesma janela, entradas | 59 respostas registradas; mediana **215 ms**, maior valor e p99 amostral **864 ms** | Indicador de lentidão de interação; amostra pequena e primeira fase sujeita a dumps. Não é p99 sustentado nem latência física da tela. |
| Estatísticas 19:35:25 | 382 redesenhos, 380 parciais; desenho médio 15,6 ms, máximo 297,9 ms | Regime diferente, com 10% da tela redesenhada em média. Uma resposta registrada de 216 ms não estabelece percentil. |
| Estatísticas 19:36:05 | Um redesenho parcial: desenho **2,0 ms**, cópia **0,03 ms**, 5 mil pixels copiados | O caminho de pequenas alterações já pode ser rápido. |

`frames=600` inclui iterações nas quais a imagem não mudou. `frame 4,8 ms` com `0 drawn` não equivale a 208 FPS de imagens novas. O tempo de quadro registrado exclui a espera final do limitador, e o monitor continuará exibindo a última imagem.

A inicialização também é uma medição distinta: primeira imagem anotada em 1,651 s após o início do processo; `load systems` leva 6,402 s; a marca lógica `ready` ocorre em 8,983 s; ainda há quadros pesados em 9,800 e 10,323 s. O wrapper já havia consumido aproximadamente 8,95 s entre seu início e a chamada de ES, segundo os uptimes. A primeira resposta anotada foi 420 ms, sem dizer quando o usuário decidiu apertar o botão. Isso não basta para medir abertura até primeira ação útil concluída.

**Etapas que podem explicar o custo**

| Etapa / localização | Evidência ou hipótese | Como separar a medição |
|---|---|---|
| Preparação da interface: `main`, input, `Window`, views e `record` | Confirmado separadamente: ES geralmente 5–43 ms nos exemplos; entrada na lista NES levou **765 ms**. Existem inicializações e cargas de imagens no thread principal. | Marcar dequeue do evento, fim de input, `update`, preparação de cada view, `render/record`, upload/decodificação síncrona e entrada de `swapBuffers`. Registrar ID da ação e da mudança de estado. |
| Detecção de regiões: `findParts` | O LCS pode visitar até aproximadamente um milhão de pares. Ao marcar mais de 70% dos tiles, o código escolhe quadro inteiro. Seu tempo não integra `esMs` nem os timers por faixa. | Cronometrar toda a função e contar comandos anteriores/atuais, pares do LCS, tiles, retângulos e motivo de fallback. Identificar qual comando alterado cobre a tela: fundo/fade legítimo ou invalidação excessiva. |
| Preparação por faixa: `executePart`, `Raster::draw/drawQuad` | Um quadro de 1080 linhas passa por cerca de **135–136 tiras de 8 linhas**, distribuídas entre os dois workers. Geometria, clipping, seleção de caminhos e procura de cache são repetidos nas tiras que intersectam cada comando. O número de chamadas não prova que elas dominam o tempo. | Contar visitas, comandos descartados por bounding box, chamadas efetivas de draw, vértices revisitados, clears e stencil resets; medir preparação separada dos spans em poucas amostras selecionadas. |
| Clears e camadas grandes | `executePart` limpa a tira antes de desenhar todos os comandos. O dump 1 contém ainda um preenchimento preto de tela inteira e uma cópia opaca de tela inteira. | Medir clear, fills e cópias opacas como categorias distintas. Contar bytes tocados e sobreposição; só depois avaliar escrita redundante com prova de cobertura/opacidade. |
| Fundos, alpha e imagens escaladas | No dump 4, dois fundos 1280×720 esticados para 1080p custaram **26,152 e 32,496 ms** na reprodução serial; outra camada 1:1 custou **16,958 ms**. Juntos representam 57% dos 132,438 ms seriais desse dump. | Agrupar tempos do caminho real por ID do comando e caminho escolhido: amostragem, cópia, modulação, alpha e bilinear direto. Registrar pixels e bytes, sem reduzir qualidade ou remover efeitos. |
| Cache de escala: `scaledFor`, `scaledRow`, `scaledFrame` | Há mutex global, orçamento padrão de 40 MiB, criação de grids e linhas preenchidas sob demanda. Cada tira pode consultar o cache; quando não há espaço, uma estrutura sem pixels persistentes é devolvida. Thrashing ou contenção são hipóteses, não fatos comprovados pelos logs. | Contar hit/miss/eviction, motivo de não retenção, bytes, linhas prontas/geradas/repetidas, alocações e tempo de espera do mutex. Separar cache frio e aquecido. A categoria `bilinear` também contabiliza o caminho com imagem escalada em cache; não significa que todos esses pixels foram interpolados novamente. |
| Texto e sombras | O dump 1 repete o mesmo texto em muitos deslocamentos. No dump 4, os comandos 39–43 mostram alpha zero na cor do primeiro vértice e ainda somam **8,198 ms** na reprodução serial. São candidatos a trabalho sem efeito visual; o dump não imprime a cor dos demais vértices. | Contar e cronometrar separadamente texto e sombras no caminho real; verificar alpha de todos os vértices. Qualquer descarte ou cache posterior exige conferência de blend/stencil e igualdade dos pixels. |
| Workers, locks e escalonamento | Os dois timers de desenho incluem o tempo em que seu thread foi desescalonado. Acordar o worker e preparar faixas ficam fora deles. Duas CPUs não garantem que memória compartilhada e preparação escalem. | Registrar publicação do job, início real de cada worker, fim do desenho, fim da cópia e retorno do join; usar relógio de CPU por thread e trocas de contexto para separar trabalho agendado de desescalonamento. Espera de memória dentro da CPU também conta como CPU time: PMU/perf seria necessário para distingui-la de execução útil. |
| Saída: `presentBand` → `presentRect` → `copyBytes` | Dumps: cópia entre **10,5 e 21,7 ms**. O benchmark isolado aponta cerca de **16,3–16,8 ms com PLD**. O código já usa formato nativo quando compatível. | Medir por worker o tempo, bytes, pitch, largura, modo nativo/conversão e tempo do caminho de conversão quando usado. Preservar o prefetch. Não confundir escrita no mmap com confirmação de apresentação física na TV. |
| Diagnóstico e pacing | Dumps refazem o quadro depois de desenhá-lo; input e `frameMs` são medidos depois dessa reprodução. O limitador pode reservar mais um intervalo de cerca de 16,67 ms após um estouro, pelo caminho que redefine `nextFrame=now`. | Medir dumps, escrita de logs e duração efetiva de `sleep_until` separadamente, além do atraso em relação ao deadline. Usar uma execução de controle sem dumps e sem profiling; não mudar o limitador antes dessa confirmação. |

**Evidência de memória: relevante, sem atribuição prematura**

Um quadro ARGB de 1920×1080 ocupa 8.294.400 bytes, cerca de 7,91 MiB. Entregar um quadro completo a 60 Hz já exige aproximadamente 498 MB/s de pixels copiados ao framebuffer, antes de contar leituras da origem, camadas e demais acessos. Isso é uma conta de volume, não uma medição do tráfego físico da DDR.

O benchmark existente, nove repetições e mediana, apresenta:

| Operação sintética | Configuração A | Configuração B | Observação |
|---|---:|---:|---|
| Uma camada com fade, uma thread | 48,7 ms, sem PLD | 28,1 ms, PLD 256 | Mostra benefício de prefetch. Duas threads com PLD deram 28,9 ms: não houve ganho nesse kernel. |
| Cópia NEON à tela, uma thread | 20,3 ms, sem PLD | 16,3 ms, com PLD | Uma cópia já fica próxima do orçamento completo de 16,67 ms de 60 Hz. Não prova um limite físico absoluto. |
| Quadro sintético, tiras 16 sem PLD → tiras 8 com PLD, uma thread | 166,9 ms | 96,9 ms | Muda dois fatores; não permite atribuir o ganho só à troca de 16 por 8. |
| Mesmo quadro, PLD fixado, tiras 16 → tiras 8, uma thread | 101,4 ms | 96,9 ms | Isola a diferença de tamanho das tiras nesse teste. |
| Quadro sintético, tiras 8 separadas e copiadas, PLD 256 | 87,9 ms, uma thread | 88,4 ms, duas threads | Sugere limitação compartilhada nessa carga. A implementação com buffer separado não é a mesma do app. |

O benchmark tem poucos kernels, dados e sequência próprios; não contém toda a UI, texto, cache, criação de listas e workers do frontend. Seus tempos não podem ser subtraídos dos 150–160 ms para descobrir um “restante” exato. Não executei um novo benchmark nesta análise.

**Por que os dumps não fecham a soma**

`dumpCommands` cria outro raster e reproduz a lista inteira em uma thread. Ele não chama `executeBand` nem reproduz as tiras de 8 linhas e a concorrência originais. Além disso, pode aproveitar o cache de escala preenchido no quadro real e altera o estado desse cache; grava os resultados em arquivo.

| Dump | Desenho real informado no cabeçalho | Soma dos comandos reproduzidos em uma thread | Cópia real informada |
|---|---:|---:|---:|
| 1 | 126,2 ms | 84,155 ms | 12,6 ms |
| 2 | 127,5 ms | 62,607 ms, total impresso | 21,7 ms |
| 3 | 206,7 ms | 77,981 ms, total impresso | 10,5 ms |
| 4 | 125,2 ms | 132,438 ms, total impresso | 11,1 ms |

Somar as linhas arredondadas do dump pode diferir alguns microssegundos do total impresso. O fato de a reprodução serial às vezes ser mais rápida **não demonstra** que remover uma thread resolveria o app. Também não autoriza atribuir a diferença à espera: mudaram ordem, cache, clipping, número de chamadas e condições de execução.

Na implementação atual, `drawing = max(desenho_worker_i)` e `copy = max(cópia_worker_i)`. Desenho de um worker e cópia do outro se sobrepõem. Portanto `max(desenho)+max(cópia)` não é a duração exata de `execute()` e pode combinar máximos de workers diferentes. A espera por despertar, preparação e parte do fechamento também precisa de marcadores próprios.

**Plano de medição antes da alteração funcional**

1. Fixar referência: binário do pacote 18 identificado por hash, 1920×1080, PLD 256, STRIP 8, duas threads, mesmo tema, biblioteca, cursor inicial e efeitos. Guardar ambiente efetivo, não só o `.ini`.
2. Fazer uma passagem normal sem dumps e sem profiler. Pelo wrapper, usar `SLOW_DUMPS=0`, `BENCH=0`, `PROFILE=0`, e garantir ausência de variáveis de diagnóstico herdadas. Não usar `ES_SOFT_BENCH=0` como substituto: o código liga esse modo pela existência da variável. Comparar separadamente uma passagem com profiling. Nenhuma mudança de qualidade.
3. Usar roteiros separados: ocioso; navegação de sistemas com fade; primeira entrada numa lista; repetição dessa lista e sistemas já visitados; rolagem prolongada. Separar primeira execução, primeira abertura da sessão e cache aquecido. Não limpar caches globais nem dados reais.
4. No primeiro nível de instrumentação, capturar apenas timestamps por quadro/worker: dequeue de evento, fim do input, fim de update, fim de record, início/fim de `findParts`, preparação das faixas, publicação do job, início/fim de cada desenho/cópia, join, fim de `execute`, manutenção do cache (`scaledFrame`) e da lista anterior, diagnóstico, início/fim de pacing. Usar o mesmo relógio monotônico (`steady_clock`, já empregado pelo renderizador) e relacionar todos ao mesmo `frame_id` e à ação que mudou o cursor ou a tela. Para CPU por thread, usar `CLOCK_THREAD_CPUTIME_ID`; registrar falta de suporte em vez de substituir por tempo de processo.
5. A linha do tempo por worker fecha o tempo real: `execute_wall = saída_execute - entrada_execute`. Comparar esse wall time com os intervalos completos de cada worker desde a publicação do job; não somar etapas sobrepostas. Separar tempo efetivo da thread de tempo fora da CPU.
6. Só depois detalhar os comandos dominantes: timers agregados por comando/categoria, visitas por tira e contadores de cache. Nada de `printf`, alocação ou relógio por pixel/grupo NEON. Usar buffers pré-alocados por thread e escrever depois da medição; amostrar o detalhamento e medir o custo da própria instrumentação contra o controle.
7. Para resposta de entrada, começar pelo timestamp do evento, medir também o tempo na fila e registrar a primeira tela **correspondente à ação**. O código atual marca a chegada ao retirar o evento e encerra a primeira entrada pendente em qualquer redesenho; isso pode agrupar comandos ou atribuir uma alteração alheia ao input. A escrita final no framebuffer é uma fronteira útil de software, mas latência até fótons depende de observação externa ou confirmação de scanout.
8. Repetir pelo menos três vezes as sequências; conservar os tempos individuais. Relatar mediana, p95/p99 quando houver amostras suficientes, máximo, quadros completos/parciais separados, atraso de deadline, memória, cache misses e filas. Para avaliar p99, buscar pelo menos 1.000 respostas úteis, sem misturar todos os cenários em uma única distribuição.
9. Avaliar primeiro redução de acessos e trabalho comprovadamente redundante: camadas opacas que tornam clears inúteis, comandos sem efeito, preparação repetida e cache. Qualquer otimização posterior deve passar por comparação de pixels (`ES_SOFT_DIRTY_CHECK=1`) numa execução distinta da de desempenho, porque esse modo desenha a referência inteira e aumenta a carga.

Não é possível decompor todos os componentes retroativamente com o binário e logs atuais. Os pontos acima definem a instrumentação mínima necessária antes de modificar o algoritmo. Os dados já coletados sustentam as prioridades, mas não uma promessa de 1080p a 60 FPS nem uma atribuição exata dos 150–160 ms a cada subetapa.

**Arquivos de evidência**

`comparacao.json`: hashes e diferenças; `comparacao-zip-instalada.diff`: delta do ZIP para a referência com patch 29; `analise-dos-logs.json`: extração verificável dos registros; `logs/`: cópias dos arquivos já coletados; `referencia-instalada/`: fontes de referência usadas na leitura. As pendências de correção funcional da auditoria permanecem registradas no relatório anterior e não foram marcadas como resolvidas por esta análise.
