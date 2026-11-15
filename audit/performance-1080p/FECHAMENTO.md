# Fechamento da otimização em 1080p — 15/11/2026

Este relatório é gerado dos resultados da matriz final. Campos sem uma execução
concluída aparecem como pendentes; não são estimativas de ganho. A aparência do
Carbon, filtragem, transparências, ordem das camadas, duração das transições,
saída 1920×1080, duas threads de desenho, STRIP=8 e PLD=256 foram preservados.
FPS=60 é o teto configurado, não o desempenho alcançado em quadros completos.

Referência reconstruída da versão já otimizada: SHA-256
`1c29852e682db65a01590674986e00c937ee14a63dc44d053700ff4a325393a6`.
Candidata combinada: SHA-256
`872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55`.
O binário instalado no início da rodada era `d53703bb…abb64`.
Os marcadores de medição são comuns aos dois builds. Não foi atribuída à
candidata a melhoria histórica contra ES_SOFT_OPTIMIZATIONS=0.

## Alterações nos fontes ativos

- Conversão RGBA→ARGB com NEON, oito pixels por vez; preserva alfa, caudas,
  sobreposição e ordem dos canais. Somente MiSTer liga o novo padrão.
- Textura comprovadamente opaca durante fade: substitui apenas o cálculo
  redundante div255(255×alfa) pelo mesmo alfa. Blend e arredondamentos restantes
  permanecem iguais. Stencil, clipping e limpeza da seleção anterior preservados.
- Segundo prefetch para a segunda linha de cache dos blocos de cópia de 64 bytes,
  mantendo PLD=256 e ordem dos stores. Não altera o formato do framebuffer.
- Catálogo limitado a duas threads no MiSTer, em vez de quatro; progresso usa
  atomic. TextureLoader usa uma thread no MiSTer; outras plataformas conservam
  seus padrões. ES_SYSTEM_THREADS e ES_TEXTURE_THREADS permitem reproduzir A/B.
- Contadores de buffers publicados em atomics evitam esperar pelo mutex do
  decoder. Custam 16 bytes por textura no ARM32, sem superfície nova. A limpeza
  de RAM de 250 MiB não é pulada só por Allocated estar abaixo de MaxVRAM.
  São contadores de buffers comprometidos; não são RSS nem orçamento global.
- Encerramento do loader pausado e sincronização de mExit corrigidos.
- Marcadores opcionais separam wrapper/helper/catálogo/primeiro quadro/entrada;
  o sleep de dois segundos e as tentativas de recuperação foram mantidos.

## Roteiro histórico na configuração atual: comparação combinada no aparelho


| Rodada | Respostas | SDL mediana ms | p95 | p99 | Máximo | Renderer completo mediana ms | RSS pico MiB |
|---|---|---|---|---|---|---|---|
| final-navigation-v1-4-reference | 1000 | 98,623 | 138,930 | 167,918 | 209,086 | 81,858 | 163,539 |
| final-navigation-current-v1-2-candidate | 1000 | 95,306 | 129,970 | 151,030 | 230,190 | 77,903 | 163,672 |
| final-navigation-current-v1-3-candidate | 1000 | 95,695 | 130,329 | 153,039 | 167,948 | 78,214 | 163,680 |
| final-navigation-current-v1-4-reference | 1000 | 97,977 | 139,279 | 181,858 | 210,078 | 81,536 | 163,691 |


ABBA, 4.000/4.000 comandos respondidos. Mediana das medianas SDL: **98,300 → 95,500 ms (2,848% de redução)**. Renderer: **81,697 → 78,059 ms (4,454%)**. Estes são ganhos incrementais desta sessão; não se subtrai o resultado de um log antigo de 150–160 ms.


| Métrica; mediana das duas rodadas | Referência | Candidata |
|---|---|---|
| p95 SDL ms | 139,105 | 130,149 |
| p99 SDL ms | 174,888 | 152,034 |
| Máximo SDL por rodada ms | 209,582 | 199,069 |


**Pior comando observado:** 210,078 → 230,190 ms; a candidata piorou esse máximo em 20,112 ms, apesar da melhora consistente de mediana/p95/p99 nas duas rodadas.



Além das respostas, foram conferidas as 4.000 mudanças de seleção esperadas no trace. Todas as versões partiram do cursor 4 e conservaram a mesma ordem/contagens de sistemas (19.284 jogos) e o mesmo hash do roteiro. Esse ponto inicial atual não substitui o cenário de abertura antigo 12→13.


| Pior evento por rodada | Comando/tecla | Cursor | UI ms | Renderer ms | Diagnósticos ms | Worker crítico: desenho parede/CPU/fora CPU ms |
|---|---|---|---|---|---|---|
| final-navigation-v1-4-reference | 2/Right | 6 | 89,634 | 103,369 | 0,035 | 94,597/50,496/44,101 |
| final-navigation-current-v1-2-candidate | 440/Left | 4 | 6,627 | 207,325 | 7,479 | 184,757/62,492/122,266 |
| final-navigation-current-v1-3-candidate | 389/Left | 7 | 5,775 | 146,154 | 0,018 | 128,090/52,527/75,563 |
| final-navigation-current-v1-4-reference | 942/Left | 6 | 8,810 | 186,240 | 4,324 | 170,559/55,361/115,199 |


O máximo de 230,190 ms ocorreu na troca de sistema do comando 440/Left. O worker crítico passou 184,757 ms desenhando, mas contabilizou 62,492 ms de CPU: aproximadamente 122,266 ms ficaram fora da CPU. Essa diferença pode incluir bloqueios/preempção; a causa específica não foi identificada. Diagnósticos consumiram 7,479 ms nesse evento. Não se atribui todo o máximo ao cálculo de alfa, prefetch ou governador sem evidência causal.



Os máximos individuais e as regressões estão expostos na tabela. Percentis das medianas de rodadas não são percentis de uma distribuição agrupada. O primeiro controle conserva seu rótulo original final-navigation-v1-4-reference; não foi copiado ou rebatizado. As três rodadas seguintes completam ABBA na mesma sessão.



Intervalos entre a última resposta de uma rodada e a primeira injeção da seguinte, em segundos: 232,953, 55,280, 61,232. Esses intervalos não são contados como latência de comando.


## Onde o tempo permanece

| Etapa: mediana em quadros completos | reference1 | candidate2 | candidate3 | reference4 |
|---|---|---|---|---|
| Preparação/update ms | 0,468 | 0,462 | 0,467 | 0,469 |
| Gravação da UI ms | 1,780 | 2,220 | 2,235 | 1,761 |
| Regiões alteradas ms | 0,104 | 0,104 | 0,104 | 0,104 |
| Maior desenho de worker ms | 73,802 | 70,936 | 71,396 | 73,405 |
| Maior cópia de worker ms | 9,637 | 8,803 | 8,692 | 9,580 |
| Manutenção posterior ms | 0,010 | 0,009 | 0,009 | 0,010 |
| Limitador da UI ms | 0,003 | 0,003 | 0,003 | 0,003 |
| Pacing do renderer ms | 0,003 | 0,003 | 0,003 | 0,003 |
| Maior parcela fora da CPU durante desenho ms | 2,407 | 2,387 | 2,379 | 2,390 |


O caminho medido é fila SDL → parseEvent → window.update → window.render
(record dos comandos) → findParts → preparação/publicação das faixas →
executePart/executeBand nos dois workers → cópia para o framebuffer → join e
manutenção. execute_ns mede o intervalo real inteiro do renderer com cópia.
Desenho de um worker pode se sobrepor à cópia de outro: não se somam máximos
independentes para reconstruir um quadro. Medianas de etapas também não formam
necessariamente o mesmo quadro. A espera na fila SDL inclui a posição da injeção
histórica no loop, não mede isoladamente o escalonador Linux.


| Rodada | Quadros desenhados | Acima de 16,7 ms | Completos |
|---|---|---|---|
| final-navigation-v1-4-reference | 5902 | 5828 | 5823 |
| final-navigation-current-v1-2-candidate | 5968 | 5914 | 5909 |
| final-navigation-current-v1-3-candidate | 5970 | 5914 | 5910 |
| final-navigation-current-v1-4-reference | 5914 | 5832 | 5829 |


Com renderer completo em cerca de 78,059 ms, alcançar 16,7 ms ainda exigiria cortar aproximadamente **61,392 ms (78,648% do restante)**. Isso explica o limite observado desta implementação; não prova um limite físico absoluto do hardware.


## Término da transição principal

| Rodada | Comandos | Término observado | Mediana até terminar ms | p95 | Intervalo mediano entre desenhos ms | Intervalos >16,7 ms |
|---|---|---|---|---|---|---|
| final-navigation-v1-4-reference | 1000 | 68 | 769,443 | 835,429 | 85,495 | 4891 |
| final-navigation-current-v1-2-candidate | 1000 | 47 | 798,715 | 823,175 | 81,785 | 4956 |
| final-navigation-current-v1-3-candidate | 1000 | 48 | 800,000 | 817,583 | 82,065 | 4958 |
| final-navigation-current-v1-4-reference | 1000 | 67 | 792,550 | 818,839 | 85,121 | 4896 |


Mede o primeiro quadro escrito com a animação principal SystemView/slot 0 inativa após uma amostra ativa, mantendo o mesmo cursor. Não inclui o fade informativo do slot 2 nem a TV. Uma nova tecla pode interromper a animação; trace apenas de redesenhos também pode omitir um estado lógico final sem alteração de pixels. Ausência de término observado não significa comando perdido. As durações acima usam um subconjunto diferente em cada versão, favorecendo intervalos longos entre comandos; não demonstram sozinhas uma alteração de duração. O cenário curto separado abaixo evita essa censura. Duração configurada e efeitos não foram alterados.


### Transições sem interrupção por nova tecla

| Rodada | Comandos/términos observados | Término mediana ms | p95 | Máximo | Intervalo de desenho mediana ms | Intervalos >16,7 ms |
|---|---|---|---|---|---|---|
| final-animations-v1-1-reference | 16/16 | 701,904 | 801,834 | 801,834 | 84,334 | 74 |
| final-animations-v1-2-candidate | 16/16 | 700,959 | 831,988 | 831,988 | 83,229 | 77 |
| final-animations-v1-3-candidate | 16/16 | 686,631 | 797,632 | 797,632 | 82,351 | 77 |
| final-animations-v1-4-reference | 16/16 | 703,916 | 781,287 | 781,287 | 83,977 | 78 |


São 16 comandos por rodada, ABBA, com 1.800 ms de espera adicional e trace de todas as iterações. A amostra final lógica fica disponível mesmo se os pixels já estiverem estáveis e não exigirem nova cópia. A métrica é o primeiro estado lógico inativo após o ativo; a TV não foi medida. Todos os términos são exigidos pelo analisador. Esse cenário mede transição completa e não substitui os percentis de 1.000 comandos da navegação contínua.


## Comparação interrompida: preservada como evidência

| Rodada | SDL mediana ms | p95 | p99 | Máximo | Renderer completo mediana ms |
|---|---|---|---|---|---|
| final-navigation-v1-1-reference | 107,523 | 154,538 | 196,461 | 264,981 | 91,468 |
| final-navigation-v1-2-candidate | 104,442 | 145,497 | 189,063 | 246,170 | 87,469 |
| final-navigation-v1-3-candidate | 104,709 | 142,932 | 174,437 | 235,800 | 87,537 |
| final-navigation-v1-4-reference | 98,623 | 138,930 | 167,918 | 209,086 | 81,858 |


As 4.000 respostas são válidas, mas a última referência foi executada depois de uma pausa longa de acompanhamento. Intervalos entre rodadas: 50,579, 51,540, 30748,684 s. A referência mudou de 107,523 para 98,623 ms; o agrupamento dessa sessão interrompida daria 103,073 contra 104,576 ms e não demonstraria ganho. Por isso foi feita uma nova comparação contínua, sem ocultar esses números. Não foi atribuído o deslocamento de desempenho ao governador ou a uma causa não medida.



## Capas frias, concorrência e memória

Mesma implementação durante o A/B isolado de uma/duas threads de imagens,
1.000 entradas compartilhando 160 PNGs originais 512×768, três ciclos de
ida/volta/carrossel/lista/menu e repouso. Todas as 3.888 entradas foram respondidas,
com um único frontend. Esse grid tem carga diferente do carrossel histórico.


| Rodada | Decoder | SDL mediana ms | p95 | p99 | Máximo | RSS pico MiB |
|---|---|---|---|---|---|---|
| covers-final-v2-1-t2 | 2 | 347,612 | 498,639 | 596,107 | 1818,877 | 212,281 |
| covers-final-v2-2-t1 | 1 | 310,051 | 448,673 | 530,290 | 1433,579 | 206,332 |
| covers-final-v2-3-t1 | 1 | 326,147 | 464,910 | 578,710 | 1355,932 | 204,883 |
| covers-final-v2-4-t2 | 2 | 370,810 | 539,440 | 631,764 | 1701,042 | 212,684 |


Mediana das rodadas: 359,211 → 318,099 ms (11,4%); p99 das rodadas:
613,936 → 554,500 ms (9,7%). Uma thread produziu menos cargas e descartes,
embora a fila pudesse ser maior: não se interpreta fila menor como resposta
automaticamente mais rápida. active_loads inclui cargas síncronas na UI.

Em teste separado de capa selecionada, visualização detailed e 24 passos
visíveis por rodada, a mediana request→upload ficou 551,805 → 554,459 ms;
p95 730,598 → 789,332 ms (**58,734 ms pior**). Todos os uploads ocorreram antes
do próximo passo de aproximadamente 910 ms. É um proxy de prontidão no software,
incluindo UI/fade/decodificação/upload; não é latência até os fótons. O piloto
rápido anterior misturava a lista oculta e não serve para essa conclusão.

A comparação curta do código anterior com o conjunto final, 52 teclas por
rodada, mediu 291,296 → 244,345 ms na mediana das rodadas. p95 passou
434,758 → 443,778 ms e p99 507,915 → 511,201 ms. São regressões pequenas
mas explícitas; com 52 amostras o p99 coincide com o máximo. O teste longo
acima isola a concorrência; os números curtos não substituem seu alcance.

O mapeamento por tecla localizou a cauda da candidata curta na saída
lista→carrossel (Escape, comando 49): 537,349 e 485,053 ms, contra
458,753 e 430,216 ms nos controles. Nessa ação, a mediana das duas rodadas
ficou 444,485 → 511,201 ms (**66,716 ms pior**); a preparação da UI na
candidata consumiu 294,998/308,006 ms e o renderer 241,950/176,767 ms.
Não se atribui esse custo inteiro a uma rotina sem marcadores adicionais.
Os resultados ficam em covers-tail-locations.json, sem nova passagem no aparelho.

Na lista longa, com 160 passos por sentido e três saídas por rodada, a mediana
das medianas de lista→carrossel foi 1.169,500 → 1.303,411 ms (piorou 133,911 ms),
enquanto o pior valor observado dessa ação caiu de 1.818,877 para 1.433,579 ms.
A rolagem melhorou, mas a saída ainda contém pausa perceptível e não é descrita
como instantânea. Há seis saídas por política, insuficientes para estimar p99
específico dessa ação. O buffer de eventos de textura saturou antes da primeira
saída nas quatro rodadas; não identifica qual decode/upload causou essa pausa.
Os tempos por ação estão em covers-long-phases.json, e a censura foi conferida
em covers-exit-observation.json. A política de um worker favorece a rolagem e
reduz o pior travamento observado, com essa troca no custo típico de saída.

Com telemetria mínima, sem trace de quadros/texturas, a candidata teve pico RSS
amostrado a 1 Hz de 192,184 MiB, VmHWM de 193,621 MiB e RSS mediano tardio
82,668 MiB no fixture. O script executou 328
teclas; CSV de inputs ficou desligado, portanto seu contador zero não representa
zero respostas. O tracing detalhado elevou o pico para cerca de 205–213 MiB.
Os eventos de textura de 4.096 posições saturaram; contadores agregados e
amostras de memória continuaram. Repetir o mesmo caminho pode significar outro
tamanho, não necessariamente uma recarga por evicção. Três ciclos não provam
ausência de vazamento em qualquer sessão de duração arbitrária.

O grid animado em repouso consumiu aproximadamente 147% de uma CPU lógica
(duas CPUs disponíveis), tanto antes como depois. Manter as animações tem custo;
esse número não representa uma tela estática nem prova 100% do hardware.


## Repouso e controles


| Rodada; 15 s de repouso | Respostas | SDL mediana ms | p95 | p99 | uinput mediana ms | CPU estática % de um core | RSS estático MiB |
|---|---|---|---|---|---|---|---|
| final-idle-v1-1-reference | 8 | 25,649 | 42,046 | 42,046 | 26,647 | 13,311 | 120,934 |
| final-idle-v1-2-candidate | 8 | 25,881 | 51,880 | 51,880 | 26,851 | 13,254 | 120,723 |
| final-idle-v1-3-candidate | 8 | 25,634 | 39,027 | 39,027 | 26,595 | 13,642 | 120,844 |
| final-idle-v1-4-reference | 8 | 28,053 | 44,682 | 44,682 | 29,341 | 13,307 | 120,961 |


| Primeira resposta após repouso | Renderer mediana ms | Respostas parciais/total |
|---|---|---|
| final-idle-v1-1-reference | 16,727 | 8/8 |
| final-idle-v1-2-candidate | 16,273 | 8/8 |
| final-idle-v1-3-candidate | 16,569 | 8/8 |
| final-idle-v1-4-reference | 17,620 | 8/8 |


Oito entradas por rodada: p99 pequeno coincide com o máximo. Repouso alterna dois sistemas adjacentes e pode responder com desenho parcial; não se compara diretamente ao percurso contínuo de quatro posições por lado. uinput inclui evdev/SDL, sem USB/radio/TV físicos.


| Rodada | Toque breve | Avanços segurando Right/Left | Parou ao soltar |
|---|---|---|---|
| final-controls-v1-1-reference | 1 | 3/4 | True |
| final-controls-v1-2-candidate | 1 | 3/5 | True |

## Abertura completa

O wrapper é o fonte ativo, em pacote isolado com os mesmos assets/configuração; a injeção começa após o primeiro quadro interativo e verifica mudança de seleção. A abertura por chamada SSH não comprova o tempo de fechamento do menu Scripts físico.


### warm: cache aquecido

| Rodada | Wrapper→entrada ms | Processo→entrada ms | Helper ms | Sleep fixo ms | Load systems s | Primeiro quadro interativo s | Seleção |
|---|---|---|---|---|---|---|---|
| startup-full-warm-v1-1-reference-1-t4 | 15529,898 | 10972,659 | 1900,000 | 2050,000 | 6,825 | 10,034 | 4→5 |
| startup-full-warm-v1-2-candidate-1-t2 | 14511,376 | 9943,172 | 1910,000 | 2070,000 | 6,386 | 9,383 | 4→5 |
| startup-full-warm-v1-3-candidate-1-t2 | 14053,337 | 9530,310 | 1870,000 | 2050,000 | 6,482 | 9,347 | 4→5 |
| startup-full-warm-v1-4-reference-1-t4 | 15605,765 | 11047,205 | 1890,000 | 2060,000 | 6,957 | 10,196 | 4→5 |


| Mediana das duas rodadas | Referência | Candidata |
|---|---|---|
| Wrapper→primeira entrada ms | 15567,832 | 14282,357 |
| Processo→primeira entrada ms | 11009,932 | 9736,741 |


| Primeira entrada após a primeira tela; mediana das duas rodadas | Referência | Candidata |
|---|---|---|
| Injeção uinput→redesenho ms | 814,682 | 299,747 |
| Fila SDL→redesenho ms | 264,348 | 199,437 |
| Primeiro quadro interativo desde o processo s | 10,115 | 9,365 |
| LoadConfig s | 6,891 | 6,434 |


A antecipação da primeira tela foi conferida junto com a primeira entrada efetivamente processada. O custo dessa navegação inicial está incluído no tempo wrapper→entrada e também aparece isolado na tabela; os ganhos não são somados. São duas execuções por versão, sem garantia de p99 de abertura.


### cold: primeira abertura após reboot real

| Rodada | Wrapper→entrada ms | Processo→entrada ms | Helper ms | Sleep fixo ms | Load systems s | Primeiro quadro interativo s | Seleção |
|---|---|---|---|---|---|---|---|
| startup-full-cold-v1-1-reference-1-t4 | 20320,503 | 15127,513 | 2300,000 | 2090,000 | 10,270 | 13,930 | 4→5 |
| startup-full-cold-v1-2-candidate-1-t2 | 20104,453 | 15125,249 | 2200,000 | 2070,000 | 10,387 | 14,057 | 4→5 |
| startup-full-cold-v1-3-candidate-1-t2 | 20318,044 | 15306,867 | 2260,000 | 2060,000 | 10,888 | 14,299 | 4→5 |
| startup-full-cold-v1-4-reference-1-t4 | 20691,789 | 15776,372 | 2220,000 | 2070,000 | 10,422 | 14,550 | 4→5 |


| Mediana das duas rodadas | Referência | Candidata |
|---|---|---|
| Wrapper→primeira entrada ms | 20506,146 | 20211,249 |
| Processo→primeira entrada ms | 15451,943 | 15216,058 |


| Primeira entrada após a primeira tela; mediana das duas rodadas | Referência | Candidata |
|---|---|---|
| Injeção uinput→redesenho ms | 632,217 | 342,598 |
| Fila SDL→redesenho ms | 269,010 | 226,492 |
| Primeiro quadro interativo desde o processo s | 14,240 | 14,178 |
| LoadConfig s | 10,346 | 10,637 |


A antecipação da primeira tela foi conferida junto com a primeira entrada efetivamente processada. O custo dessa navegação inicial está incluído no tempo wrapper→entrada e também aparece isolado na tabela; os ganhos não são somados. São duas execuções por versão, sem garantia de p99 de abertura.



Novo boot_id foi exigido por rodada; nenhum hash/pré-leitura do ELF antes de iniciar o processo. Os serviços originais permaneceram ativos e receberam o mesmo intervalo de estabilização. O reboot não é drop_caches.



## Qualidade, estabilidade e alcance dos testes

Host com ASan/UBSan/LSan e ARM: 112 cenas de rasterização contra código
anterior independente, 7.027 casos de conversão RGBA e 14 cenas de integração
do backend; 64 ciclos de vida do loader incluindo pausa/encerramento e fila.
ARM32 reproduziu a falha da limpeza de RAM anterior e confirmou a correção,
também quando Allocated excedia 250 MiB mas RAM permanecia abaixo do limiar.
O ramo de contabilidade GLES3 compilou; não foi validado em execução noutra
plataforma. A cobertura não equivale a testar todos os temas existentes.

No aplicativo ARM, capturas de carrossel/lista/seleção/menu/transição em estados
lógicos iguais tiveram igualdade de pixels, incluindo 18/18 SHA-256 iguais na
passagem de animação e zero divergências parcial/completo. A passagem de
qualidade usa relógio lógico fixo somente em build próprio; não é o executável
distribuído nem entra nas medições de desempenho. A cópia adicional teve todos
os 2.073.600 pixels do framebuffer verificados após cada microensaio.

O congelamento comunicado pelo usuário foi investigado: OSD continuava vivo,
SSH retornou, e dmesg confirmou OOM após a bancada sobrepor dois frontends.
Foi um erro de sincronização do restore da bancada, corrigido e validado em
duas regressões antes de retomar os testes. A rodada inválida não entra em
nenhuma tabela. Não foi ocultado como um problema de governador ou hardware.



Retorno de core, passagem de qualidade: 91 quadros conferidos, 0 divergências. A conexão falhou antes de enviar MENU; NES e PID foram conferidos antes de recuperar o retorno. O core permaneceu ativo por cerca de 157 s, em vez dos oito planejados. Esse intervalo e a verificação dupla não são tratados como desempenho normal. A primeira contagem de inputs não demonstrava ações úteis; isso foi corrigido na passagem seguinte por verificação explícita do estado/seleção.


### Retorno normal com o programa original de teste

| Etapa medida no MiSTer | Tempo ms |
|---|---|
| Espera fixa após MENU observado pelo launcher | 2050,000 |
| Ativação do framebuffer pelo helper | 1890,000 |
| Helper concluído→primeiro desenho | 2730,101 |
| MENU observado pelo launcher→primeiro desenho | 6710,101 |


Verificação dupla, PNG, slow dumps e profiler desligados; mesmo binário candidato em 1080p. O catálogo deste teste é mínimo, com um programa NES original sem save RAM: não é uma medição de ganho antes/depois no catálogo real. Houve uma única abertura de core. Após voltar, cinco probes confirmaram carrossel ativo e seleção 0→1→0 com as setas.

O roteiro atrasou deliberadamente esses controles e esperou uma condição de repouso que o grid animado não atingia. O acompanhamento expirou antes do roteiro, mas a execução autônoma terminou e foi coletada com os estados esperados. O primeiro desenho acima foi medido antes dessa espera; o timestamp tardio dos controles não estima a menor prontidão de entrada. O roteiro de reprodução foi corrigido para usar a espera de segurança e probes de seleção, sem exigir que um grid animado pare de redesenhar. A menor prontidão de entrada após retorno permanece não medida.

A tentativa final-core-speed-v1 foi invalidada: um Return enviado antes de confirmar a tela reabriu o próprio programa de teste. O retorno foi recuperado após conferir NES/PID, sem outro frontend; seus números não entram na aceitação. Os artefatos foram preservados.


## Camadas: perfil detalhado separado

| Grupo por dimensões/vértices | Amostras comando/worker | Visitas em faixas | Soma de parede dos workers ms | Participação nessa soma % |
|---|---|---|---|---|
| imagem grande, primeiro alfa parcial | 48 | 3260 | 797,338 | 36,257 |
| imagem grande, primeiro alfa 255 | 54 | 3546 | 541,978 | 24,645 |
| imagem menor, primeiro alfa parcial | 250 | 1806 | 444,771 | 20,225 |
| atlas/texto ou geometria com mais de quatro vertices | 173 | 1540 | 268,027 | 12,188 |
| retangulo sem textura, primeiro alfa parcial | 38 | 627 | 81,437 | 3,703 |
| imagem menor, primeiro alfa 255 | 147 | 761 | 53,027 | 2,411 |
| retangulo sem textura, primeiro alfa 255 | 6 | 288 | 12,578 | 0,572 |


Esse perfil anterior amostra quadros selecionados. Imagens grandes somam aproximadamente 60,9%; o grupo de atlas/texto/geometria, 12,2%. A soma dos dois workers não é o tempo real do quadro, pode conter pausas fora da CPU e inclui custo dos marcadores detalhados. Primeiro alfa não prova opacidade da textura. Esses grupos permitem priorizar composição e imagens, mas não atribuir um custo semântico exato a cada fundo/capa/overlay sem mapear o asset. Não foram somados aos tempos dos testes rápidos.



## Por que esta rodada termina aqui

As duas últimas hipóteses de renderer foram encerradas: o segundo prefetch
da cópia teve ganho pequeno reproduzido; busca NEON de fronteiras de alfa foi
rejeitada porque o ganho de componente não virou melhora reproduzível da
abertura. Subdivisão horizontal, fusão de quads, blend empacotado e amostragem
NEON fria também foram rejeitados. Aumentar cache de 40 para 48 MiB não removeu
misses adicionais; prefixo de camadas reaproveitável foi zero nos 41 quadros
completos investigados. Um framebuffer 1080p custa 7,91 MiB, além do custo de
construção, invalidação e cópia. Não foi criado um cache de tela inteira sem
conteúdo estável demonstrado.

O perfil detalhado separado concentra custo em imagens grandes e composição
com transparência; texto ainda visita muitas faixas. Dimensões/primeiro alfa
identificam grupos, não o nome semântico de cada asset. Não se confunde soma
dos tempos dos dois workers com parede do quadro, nem se usa tempo de dump
serial como se fosse o renderer paralelo. A referência e a candidata continuam
precisando ler/amostrar/compor várias vezes parte dos mesmos pixels.

O Linux 6.18.38-MiSTer expõe dois Cortex-A9/NEON, 491,08 MiB de RAM e nenhuma
swap. Não expõe políticas CPUFreq/driver/governador/frequências ajustáveis;
o A/B performance é **não medido por ausência de controle**. Não se carregou
o módulo de overclock opt-in nem se alterou permanentemente kernel ou VM.
CPU/PMU/DDR não foram medidas a ponto de provar saturação física; a PMU existe
em sysfs, mas não houve coleta de ciclos/instruções/misses nesta matriz.
Alguns máximos incluem tempo fora da CPU cuja causa exata permanece incerta.
Consulte LINUX.md e LIMITES.md.

Outra arquitetura poderia estudar compositor acelerado, reutilização com
invalidação mais sofisticada ou apresentação que evite cópias, mas isso exige
projeto e validação próprios. Não é um ganho já demonstrado nem uma promessa
de 16,7 ms mantendo exatamente a aparência. O FPGA atualmente não executa um
compositor da UI deste frontend.

## Reprodução e pendências explícitas

Comandos exatos e condições estão em REPRODUZIR.md e nos controladores de
validation/. Os JSONs por rodada preservam hashes, parâmetros efetivos e
amostras; medições no host são identificadas separadamente das do MiSTer.
Não foi medida latência física controle→TV, nem o percurso real pelo menu
Scripts. Nenhuma medição do wrapper por SSH substitui essas verificações.
O menor tempo de entrada após retorno de core também não foi estimado pelo
roteiro funcional com espera deliberada. As primeiras entradas da abertura
inicial aquecida/após reboot foram medidas com injeção antecipada e seleção real.



## Instalação

Registro verificado: `deployment-final-20261115.json`.

```json
{
  "backup": "/media/fat/emulationstation/backups/codex-1080p-final-20261115-2556539896074",
  "original_sha256": {
    "LEIA-ME.txt": "aba3f84c1c916291750a97e893e80e90e581fb50362b22b304c40164750c72f2",
    "emulationstation": "d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64",
    "es-mister-helper": "c826dc9341aa47947e25230d8be51ca1b80882a4dc8e11296364d8bb785288b8",
    "es-mister.ini": "cdaa7045899ab22229bd698796c9dc122b907b40d5fb11118baf77bd129ce627",
    "es-mister.sh": "dbaecd4e2b5d9d357f89fe14672bbbb9f1eebcd55d4a57a1d930936a0bde4787",
    "mister-launch.sh": "a1c94434196806b2c76ac297285ae044e7ea1cf9e7c98eb448f675d2f1a2bf25"
  },
  "installed_sha256": {
    "emulationstation": "872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55",
    "es-mister-helper": "c826dc9341aa47947e25230d8be51ca1b80882a4dc8e11296364d8bb785288b8",
    "es-mister.sh": "73ee5b7ea6b26b18fddfaf64599cb09eabcda59cce5da0964e0dffa6d5d9da15",
    "mister-launch.sh": "6202964a729bf5d3da8e8d02e7450fb5138254d1a1214533a73f61953b7fa5f4",
    "LEIA-ME.txt": "aba3f84c1c916291750a97e893e80e90e581fb50362b22b304c40164750c72f2"
  },
  "settings": {
    "SIZE": "1920x1080",
    "FPS": 60,
    "STRIP": 8,
    "PLD": 256,
    "BENCH": 0,
    "PROFILE": 0,
    "SLOW_DUMPS": 0
  },
  "launcher_pid": 8011,
  "frontend_pid": 8420,
  "api_ready": true,
  "framebuffer": "1920,1080"
}
```



Verificação da execução normal: PID 8420, um frontend e um wrapper, primeiro quadro interativo confirmado, 1920×1080×32 bits, Carbon `es-theme-carbon`, STRIP=8/PLD=256/FPS=60. Trace de quadros/texturas, verificação dupla, PNG/roteiro e profiler desligados. SHA-256 instalado confere com a candidata medida. OOM deste boot: ['oom_kill 0']; políticas CPUFreq: []. A cópia anterior e seus hashes estão no backup acima.



Observação da execução instalada sem roteiro/trace, por 10,031 s: RSS 182,008–182,008 MiB, VmHWM 205,723 MiB e CPU 10,268% de um core. O usuário experimentou o controle nesse período; não se trata de repouso controlado, comparação antes/depois de CPU ou prova de estabilidade ilimitada. O benchmark de repouso acima usava roteiro ativo e não deve ser confundido com essa observação normal.



Validação física informada pelo usuário em 15/11: **controle funcionando normalmente na TV**. Áudio do frontend: **sem som, como antes**, sem regressão relatada. Isso confirma funcionamento observado; não mede controle→fótons. O áudio de jogos não foi validado pelo programa de vídeo silencioso.
