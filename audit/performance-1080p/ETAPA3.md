# Etapa 3 — decisões e histórico de validação, 15/11/2026

**O estado consolidado, as tabelas completas e as pendências estão em [FECHAMENTO.md](FECHAMENTO.md).**
Este arquivo preserva a sequência de decisões e os ensaios isolados.
O primeiro teste com capas encontrou perda de SSH e imagem congelada. O acesso
retornou e o dmesg confirmou OOM com duas instâncias do frontend: o restore da
bancada retornava antes de o launcher normal criar seu processo, permitindo
iniciar a candidata ao mesmo tempo. A sincronização foi corrigida nos utilitários
de teste; essa sessão foi invalidada. Não é evidência de um limite físico do
hardware. A regressão da bancada e a aceitação continuam antes de qualquer deploy.
No início, o aparelho conservava o binário validado `d53703bb…abb64`, Carbon em 1920×1080,
duas threads de renderização, STRIP=8, PLD=256 e FPS=60. Não houve ajuste de
kernel. A investigação concluída do Linux está em [LINUX.md](LINUX.md).

## Resultados incrementais já observados no ARM

| Alteração isolada | Controle | Candidata | Alcance da evidência |
|---|---:|---:|---|
| Catálogo: quatro → duas threads; processo → primeira entrada respondida | 9.864 s | 9.225 s | Seis aberturas aquecidas por política, ordem alternada; seleção 12→13 verificada em todas |
| Conversão RGBA→ARGB, superfície 1080p opaca | 54,82–54,89 ms | 26,42–26,48 ms | Microbenchmark ARM, oito pixels por vez; não é o tempo de um quadro |
| Conversão RGBA→ARGB, alfa 127 | 43,28–43,32 ms | 26,47–26,59 ms | Mesmo microbenchmark ARM |
| Conversão RGBA no aplicativo: processo → primeira entrada respondida | 11.289,523 ms | 10.508,790 ms | Seis aberturas aquecidas por opção, ABBA repetido três vezes; seleção 12→13 em todas |
| Conversão RGBA no aplicativo: uinput → primeiro redesenho | 658,915 ms | 288,500 ms | Primeiro comando antecipado; inclui a fila antes do primeiro desenho útil |
| Alfa conhecido de textura opaca: renderer completo | 90,8255 ms | 87,2005 ms | Mediana das medianas de duas rodadas de 1.000 comandos por opção |
| Mesma alteração: fila SDL → primeiro redesenho | 104,7305 ms | 102,0535 ms | Quatro rodadas ABBA; 4.000/4.000 respostas |

As duas linhas de navegação usam **o mesmo executável**, com somente
`ES_SOFT_OPAQUE_FADE=0/1` alterado. SystemData e recursos de textura desse
executável foram reconstruídos a partir de cba2b66, a versão já otimizada.
Não se atribui à candidata o ganho histórico contra OPTIMIZATIONS=0.

As quatro rodadas longas da alteração de alfa estão concluídas. São medições
incrementais da mesma versão já otimizada, sem usar OPTIMIZATIONS=0 como base.

| Rodada, ordem ABBA | Mediana SDL (ms) | p95 (ms) | p99 (ms) | Máximo (ms) | Mediana renderer completo (ms) |
|---|---:|---:|---:|---:|---:|
| A1, anterior no mesmo executável | 104,045 | 149,007 | 181,988 | 212,040 | 90,387 |
| B1, alfa opaco ligado | 101,844 | 143,474 | 174,809 | 188,995 | 87,327 |
| B2, alfa opaco ligado | 102,263 | 146,481 | 181,170 | 236,987 | 87,074 |
| A2, anterior no mesmo executável | 105,416 | 149,418 | 181,896 | 196,366 | 91,263 |

O ganho entre medianas das rodadas foi 2,56% na primeira resposta e 3,99% no
renderer. A pior ocorrência de B2 aumentou o máximo de entrada em 24,947 ms
frente ao pior controle; o máximo do renderer passou de 229,334 para 299,778 ms.
Nesse quadro, o caminho crítico teve 289,983 ms de parede e 166,362 ms de CPU.
Há aproximadamente 124 ms sem execução nessa thread; os dados não identificam
qual escalonamento, interrupção ou serviço causou a pausa. Não se atribui esse
máximo ao governador ou à alteração de alfa sem evidência causal.
O RSS máximo com tracing ficou entre 163,1 e 163,5 MiB. A decisão final depende
também da comparação combinada, sem ocultar essa troca no máximo observado.

Uma comparação adicional de 64 comandos por rodada contra SoftRaster realmente
extraído de cba2b66 confirmou a redução: medianas SDL A1/B1/B2/A2 de
109,293/102,867/103,416/107,361 ms; renderer 92,076/87,118/88,126/90,124 ms.
Todos os 256 comandos foram respondidos. O analisador foi corrigido para
associar quadros pelo instante exato do despacho, mantendo a origem SDL
apenas para a latência; isso evita selecionar o quadro ocioso anterior por
arredondamento de um milissegundo. Nessa amostra, o término observado da
animação principal ficou perto de 658–665 ms; intervalos entre quadros ficaram
perto de 91–96 ms. O evento da primeira resposta e o fim da transição são
métricas distintas. Transições interrompidas não são comandos perdidos.

O ganho curto não aproxima redesenhos completos de 16,7 ms. O teto configurado
de 60 FPS continua sendo um limitador, sem demonstrar 60 quadros completos/s.
Os tempos de entrada medem software até a escrita do primeiro redesenho, não
latência física controle–TV nem término da transição.

## Fidelidade e correções

- Rasterizador: 112 cenas determinísticas iguais à implementação independente
  extraída de cba2b66, no host com ASan/UBSan/LSan e no ARM. Foram mantidos
  prefetch e faixas de oito linhas. O novo caminho elimina somente o cálculo
  redundante `div255(255 * alpha)`, preservando os demais arredondamentos.
- Aplicativo, alfa opaco desligado/ligado: PNGs de carrossel, lista, seleção e
  menu com SHA-256 idênticos. Redesenho parcial versus completo: 0/88 e 0/86
  divergências. Em diagnóstico separado, a candidata processou 136.192.320
  pixels pelo novo caminho durante transições. Esses testes têm tracing detalhado
  ou verificação dupla; seus tempos não são usados como teste de velocidade.
- Conversão RGBA: 7.027 casos diferenciais no ARM e host, incluindo caudas,
  sobreposição, alinhamentos, alfa e sentinelas. Referência extraída do código
  anterior, sem compartilhar a função candidata. Integração do backend também
  passou em 14 cenas ARM independentes. No aplicativo, 18 capturas de estados
  lógicos equivalentes tiveram SHA-256 idênticos, incluindo oito estados de
  transição e oito de seleção; 0/60 e 0/63 divergências parcial/completo.
  PNG e verificações duplas ficaram desligados nas medições de velocidade.
  Os fontes preparam o caminho por padrão somente no build MiSTer candidato,
  respeitando OPTIMIZATIONS=0; produção anterior permanece preservada enquanto
  a aceitação conjunta não estiver concluída. Outras plataformas mantêm o padrão.
- TextureLoader: saída com carregamento pausado e notificação de encerramento
  corrigidas; 64 ciclos de criação/encerramento e conclusão da fila passaram no
  host e ARM. Após o A/B com capas, o padrão do MiSTer passou a uma thread de decodificação;
  as demais plataformas conservam seus padrões.
- Limpeza: limite de RAM de 250 MiB em plataformas de 32 bits passa a ser
  independente do retorno antecipado de MaxVRAM. Teste ARM reproduziu a falha
  anterior com MaxVRAM=512 MiB e confirmou a correção. Com o padrão de 128 MiB,
  esse caso não é uma explicação demonstrada da latência normal.
- A contabilidade passa a ler tamanhos publicados em atomics, sem esperar pelo
  mutex de um decoder. O teste ARM confirmou leitura do valor anterior durante
  uma escrita bloqueada e do novo valor após sua publicação. São 16 bytes de
  metadados por textura no ARM32, sem outra superfície. Os contadores preservam
  a semântica de buffers comprometidos; não representam RSS ou temporários de
  decodificação. O ramo GLES3 compilou, mas não foi executado em outra plataforma.

Um protótipo intermediário que bloqueava getMemoryUsage pelo mutex foi
descartado: introduzia esperas de até 153 ms. Remover esse bloqueio não constitui
ganho contra a versão cba2b66, que nunca o continha.

## Duas hipóteses finais e controle da qualidade

O prefetch adicional preserva PLD=256, os stores e a ordem de apresentação.
Em quatro execuções curtas ABBA de 64 comandos, as medianas do renderer foram
87,401/84,946/86,135/87,133 ms. A mediana das medianas passou de 87,267 para
85,541 ms. As medianas SDL foram 102,604/101,747/103,199/105,169 ms, com
256/256 respostas. Os p99 de entrada da candidata (176,178 e 171,955 ms)
superaram os dos controles (162,434 e 159,409 ms); não se oculta essa variação
em uma amostra curta. No componente isolado, a cópia ganhou cerca de 0,45 ms.
Foram verificados todos os 2.073.600 pixels do framebuffer após cada ensaio.

A busca NEON de fronteiras de alfa passou em 1.632 metadados independentes e
155.376 casos de limites com guardas no host e ARM. Um primeiro protótipo
piorou imagens com muitos detalhes de alfa; o caminho escalar foi preservado
para esses casos antes da repetição. A medição de componente melhorou os quatro
padrões. Isso ainda não demonstra um ganho no aplicativo ou na abertura.

A comparação de abertura foi então concluída: seis execuções por opção, ABBA
repetido três vezes, mesmo binário e upload NEON ligado. Processo → primeira
entrada respondida: 10.719,855 ms no controle e 10.793,828 ms na candidata;
máximos 11.098,923 e 11.449,107 ms. SDL → primeiro redesenho: medianas
190,922 e 223,540 ms; máximos 391,369 e 588,064 ms. Uinput → primeiro
redesenho: medianas 475,850 e 459,466 ms; máximos 651,461 e 848,210 ms.
Todos os 12 comandos mudaram a seleção 12→13. A variação entre quadros iniciais
impede atribuir cada diferença ao scan; o ganho de componente não demonstrou
melhora reproduzível no objetivo de abertura. **Hipótese rejeitada.** A função
ativa findClearRuns foi restaurada da referência verificada. O protótipo e seus
testes ficam em `tests/audit/experiments/alpha-scan/`, fora do build distribuído.

Na primeira comparação visual, 16/18 PNGs coincidiram; dois quadros durante a
entrada na lista diferiram em até dois níveis por canal. Não foram aceitos como
equivalentes apenas por a diferença ser pequena. Um build **somente de qualidade**
passou a fornecer passos lógicos de 100 ms, independentemente do custo de PNG e
carregamento. Esse build também comparou, em cada upload real, os metadados de
alfa com o algoritmo anterior independente, sem copiar os pixels da superfície.
Resultado: **18/18 SHA-256 idênticos**, 71 verificações de metadados em cada
execução e 0/61 e 0/60 divergências parcial/completo. A origem era o instante da
animação capturado. O relógio fixo só existe no build de qualidade; não entra nos
fontes ativos ou nas medições de velocidade. A variável também é recusada pelos
analisadores de velocidade.

O compromisso de explicar o limite, sem confundi-lo com o limite físico do SoC,
está detalhado em [LIMITES.md](LIMITES.md). A matriz foi encerrada em 15/11, e a candidata `872dd340…65b55` foi instalada
com backup. Os limites e as verificações físicas pendentes constam no fechamento.

## Capas — comparação concluída

As quatro rodadas válidas `covers-final-v2-*` concluíram 3.888/3.888 respostas,
com um único frontend e sem novo OOM. Uma thread de imagens reduziu a mediana
das rodadas de 359,211 para 318,099 ms e o p99 de 613,936 para 554,500 ms.
O pico RSS passou de aproximadamente 212–213 para 205–206 MiB com tracing.
A fila pode ser maior com um worker, mas houve menos cargas e descartes.

O fixture possui 1.000 entradas compartilhando 160 PNGs de 512×768. O catálogo
real NES possui 2.465 arquivos; as 45 entradas de seu gamelist não possuem capas.
Não se comparam os números do grid sintético diretamente ao carrossel histórico.

No cenário separado de capa selecionada visível, a mediana request→upload ficou
praticamente igual (551,805 → 554,459 ms), mas p95 piorou 58,734 ms. A troca
foi mantida explícita. A passagem com telemetria mínima teve pico RSS de
192,184 MiB. A tabela por rodada, os contadores e as limitações de identificação
estão em [FECHAMENTO.md](FECHAMENTO.md), com JSONs brutos na entrega de evidências.

## Hipóteses descartadas ou ainda limitadas

Subdivisão horizontal, fusão de quads, blend com canais empacotados e amostragem
NEON fria foram descartados após medições. O último melhorava um microbenchmark,
mas aumentou a mediana da primeira resposta em 177 ms e o máximo da abertura em
509 ms. Não foi instalado. Os patches ficam no histórico da bancada.

Um perfil detalhado das oito primeiras navegações encontrou zero comandos
iniciais consecutivos reaproveitáveis em cada um dos 41 quadros completos
observados. Um cache simples do prefixo das camadas não ajudaria nesse trecho.
Isso não prova que todo cache seja inútil. Uma superfície 1080p de 32 bits
consome 8.294.400 bytes (7,91 MiB), além do custo de construção, cópia e invalidação.

As esperas do launcher foram instrumentadas de forma opcional com
`ES_WRAPPER_TRACE=/caminho`. O sleep de dois segundos e a recuperação por
tentativas permanecem. Ainda faltam medições reais dos percursos Scripts,
retorno de core e abertura após reboot; os números de abertura acima começam
no processo, depois da preparação do helper na bancada.

## Reprodução da bancada atual

Executar a partir de `es-mister-audit` no PowerShell, usando o cache ARM já
preparado e o SSH configurado. Estes nomes precisam ser novos a cada execução.

```powershell
wsl -d Ubuntu-26.04 -u root -- bash ./validation/cross_cache.sh bash ./validation/cross_build.sh opaque-only diagnostic-reference
wsl -d Ubuntu-26.04 -- arm-linux-gnueabihf-strip -o ./validation/arm/emulationstation-opaque-only-stripped ./validation/arm/emulationstation-opaque-only
wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py upload ./validation/arm/emulationstation-opaque-only-stripped stage3-opaque-only
pwsh -NoProfile -File ./validation/opaque_compare.ps1 -Prefix novo-quality -Order 0,1 -Quality -Detail 16
pwsh -NoProfile -File ./validation/opaque_compare.ps1 -Prefix novo-long -Order 0,1,1,0 -Steps 1000
```

`cross_build.sh` chama o `mister/build.sh es` dos fontes preparados, registra
os hashes dos arquivos usados e espera a compilação terminar antes de copiar o
ELF. A variante diagnostic-reference conserva os recursos/SystemData de cba2b66;
`active` compila todos os fontes ativos. O build ARM é estático para
arm-linux-musleabihf/Cortex-A9. Não se deve medir antes do término de build/upload.

As execuções rápidas usam OPTIMIZATIONS=1, sem PNG, verificação dupla, dumps,
redesenho forçado ou profiler. O trace de quadros foi limitado a redesenhos para
caber a navegação completa no buffer; sua saturação é verificada pelo analisador.
Os 1.000 inputs continuam registrados em buffer separado.

## Matriz final

A instalação final, a execução normal em 1080p e seus hashes foram conferidos.
A investigação desta rodada está encerrada; as limitações de medição permanecem
explícitas em FECHAMENTO.md.

A exploração das duas hipóteses finais terminou: prefetch da cópia passa pela
aceitação conjunta e busca NEON de alfa foi rejeitada. Não abrir outra hipótese.
O A/B de decoders, a medição mínima de memória e a comparação curta dos fontes
com capas foram concluídos. A navegação longa precisou ser repetida na mesma
sessão após mudança importante do controle entre dias; a sessão interrompida
foi preservada, sem lhe atribuir ganho.

O estado de controles, repouso, abertura aquecida/após reboot, retorno de core,
instalação e inspeção física está em [FECHAMENTO.md](FECHAMENTO.md). Os comandos
completos estão em [REPRODUZIR.md](REPRODUZIR.md).
