# Limite observado desta rodada

As medições e o estado da aceitação estão em [FECHAMENTO.md](FECHAMENTO.md).
Este texto explica o critério de encerramento, sem afirmar um teto físico do SoC.
A exploração das duas hipóteses finais terminou; a matriz restante valida o
conjunto, sem abrir uma terceira hipótese de renderer.

## O que foi demonstrado

A maior parcela está na rasterização/composição e cópia, não no cálculo de
regiões alteradas ou numa espera de 16,7 ms acrescentada a cada quadro atrasado.
A referência histórica otimizada já tinha duas threads, NEON, PLD=256, faixas de
oito linhas, descarte condicionado de comandos transparentes, cache de escala,
redesenho parcial e correção do pacing. Não se atribui novamente esse ganho à
etapa final.

A alteração isolada de alfa opaco levou a mediana do renderer completo de
90,8255 para 87,2005 ms nas medianas de duas rodadas por opção. A cópia isolada
com dois workers ficou perto de 12 ms; o segundo prefetch economizou cerca de
0,45 ms nesse componente. Os resultados combinados devem ser lidos na tabela
atual: ganhos isolados de catálogo, upload, fade e cópia não podem ser somados.

Houve mudança importante do controle depois da interrupção entre dias: a
primeira sessão passou de 107,523 para 98,623 ms na referência. Essa sessão foi
preservada, e uma comparação contínua foi feita para não anunciar uma melhora
menor que a variação do controle. Isso é um limite de atribuição experimental,
não uma falha que justifique ocultar os números.

## O orçamento de 16,7 ms

Um framebuffer de 1920×1080×4 ocupa 8.294.400 bytes, ou 7,91 MiB. Só os bytes
escritos de uma apresentação completa a 60 Hz totalizam cerca de 498 MB/s
(decimais), antes de ler a origem, amostrar imagens ou compor camadas. É volume
lógico, não tráfego físico DDR medido. Cache e reutilização de linhas mudam os
acessos efetivos à memória.

Um blend típico precisa ler origem e destino e escrever destino. Bilinear
pode consultar quatro texels por pixel, com reutilização importante. Não se
multiplicou esse volume por todos os pixels para afirmar saturação da DDR.
No roteiro do carrossel, as estatísticas registraram aproximadamente 5,2–5,4
milhões de pixels processados por quadro desenhado, além de quase dois milhões
copiados. Isso evidencia trabalho repetido, mas não mede isoladamente o limite
físico de leitura, escrita ou instruções do processador.

Quando o renderer completo custa aproximadamente 87 ms, o alvo de 16,7 ms
exige remover mais de 80% do tempo restante. Ganhar 0,45 ms de cópia ou cerca de
2 ms na preparação não fecha essa diferença. Uma primeira resposta parcial
pode ficar perto de um orçamento de quadro mesmo quando o restante da transição
precisa de redesenhos completos muito mais caros. FPS configurado, FPS de
iterações sem desenho, primeira resposta, fim da animação e controle→fótons
são medidas distintas.

## Por que não bastou aumentar cache ou trocar o kernel

O perfil detalhado separado concentrou aproximadamente 60,9% da soma de tempos
dos workers em grupos de imagens grandes; atlas/texto/geometria com muitos
vértices ficou em aproximadamente 12,2%. Soma de workers inclui sobreposição e
pode incluir pausas fora da CPU; não é parede do quadro nem identificação exata
do nome de cada asset. Nos 41 quadros completos amostrados, o prefixo inicial
de comandos estáveis reaproveitáveis foi zero. O cache escalonado de 40 para
48 MiB conservou os mesmos seis misses no trecho observado. Essas propostas
concretas foram rejeitadas; isso não prova que qualquer cache futuro seja inútil.

Uma superfície adicional de tela inteira custa mais 7,91 MiB, fora a textura,
metadados, invalidação, reconstrução e cópia. Sem estabilidade e hits medidos,
criar esse cache apenas deslocaria trabalho e pressão de memória.

Linux 6.18.38-MiSTer, dois Cortex-A9/NEON, 491,08 MiB de RAM e swap ausente. O
kernel não expôs políticas CPUFreq, driver, governador ativo ou frequências
permitidas pela interface consultada, também sob carga. Não houve um A/B de
governador: o ganho é não medido por falta desse controle. O padrão compilado
performance não comprova um governador ativo. O módulo opt-in de overclock não
foi carregado e nenhum ajuste permanente de kernel/VM foi feito.

A PMU armv7_cortex_a9 apareceu em sysfs, com perf_event_paranoid=2. Não foram
coletados ciclos/instruções/misses nesta matriz. Tempo de CPU inclui stalls de
memória; tempo fora da CPU não revela sozinho qual interrupção, serviço ou
escalonamento causou uma pausa. Não há prova de 100% de utilização de todos os
recursos, nem de saturação da DDR. O OOM do incidente com dois frontends foi
provocado pela bancada e corrigido; não explica a navegação de uma instância.

## Onde a rodada para

Busca NEON de alfa, subdivisão horizontal, fusão de quads, blend empacotado e
amostragem NEON fria não demonstraram ganho suficiente no aplicativo e foram
rejeitados. Conversão RGBA, alfa opaco e prefetch têm diferenciais de pixels
independentes; a aceitação conjunta verifica comportamento e as caudas da
latência. A política de um decoder melhora o grid com capas, mas piorou em
58,734 ms o p95 do proxy de disponibilidade da capa selecionada em teste
separado. Essa troca fica explícita, sem alegar melhora universal.

O limite desta rodada é o melhor resultado demonstrado mantendo aparência,
controles e arquitetura atual. Não é uma otimização perfeita nem uma prova de
impossibilidade física dos 16,7 ms. Um compositor acelerado, reutilização mais
profunda com invalidação correta ou outra apresentação poderia exigir novo
projeto. O renderer atual rasteriza a UI no ARM; não usa o FPGA como compositor
da interface. Nenhuma dessas alternativas foi implementada ou medida aqui.

A tabela final registra mediana, p95, p99, máximos, respostas, quadros acima de
16,7 ms, RSS e CPU; as limitações do host/ARM e as verificações físicas ainda não
observadas permanecem identificadas. O projeto não será descrito como 60 FPS
em redesenhos completos se a medição não sustentar essa afirmação.

## Encerramento da matriz

A candidata foi instalada e verificada em 15/11/2026. Na comparação contínua,
renderer completo: 81,697 → 78,059 ms; fila SDL → primeira resposta: 98,300 →
95,500 ms; p99 das rodadas: 174,888 → 152,034 ms. O pior comando observado
subiu de 210,078 para 230,190 ms e permanece exposto no fechamento.
O usuário confirmou controle normal na TV e frontend sem som, como antes.
Não foram medidos controle→fótons, o início real pelo menu Scripts ou a menor
prontidão de entrada após retorno de core. O limite é desta rodada, não um
teto físico comprovado nem uma promessa de 16,7 ms.
