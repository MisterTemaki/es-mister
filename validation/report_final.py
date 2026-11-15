"""Build the final Portuguese report from accepted raw result files; no target I/O."""
import csv,json,pathlib,statistics
V=pathlib.Path(__file__).resolve().parent
D=V.parent/'es-mister/audit/performance-1080p'
def read(name):return json.loads((V/name).read_text())
def f(value):return f'{value:.3f}'.replace('.',',')
def table(head,rows):return '\n'.join(['| '+' | '.join(head)+' |','|'+'|'.join('---' for _ in head)+'|']+['| '+' | '.join(map(str,row))+' |' for row in rows])
def median(values):return statistics.median(values)
parts=['''# Fechamento da otimização em 1080p — 15/11/2026

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
''']
navfile=V/'final-navigation-current-v1-summary.json'
if navfile.exists():
    nav=read(navfile.name);runs=nav['runs']
    parts.append(table(['Rodada','Respostas','SDL mediana ms','p95','p99','Máximo','Renderer completo mediana ms','RSS pico MiB'],[
        [r['run'],r['responded'],*[f(r['sdl_first_redraw_ms'][k]) for k in ('median','p95','p99','max')],f(r['full_renderer_ms']['median']),f(r['rss_peak_mib'])] for r in runs]))
    grouped={kind:[r for r in runs if r['implementation']==kind] for kind in ('reference','candidate')}
    a,b=[median(r['sdl_first_redraw_ms']['median'] for r in grouped[k]) for k in grouped]
    ar,br=[median(r['full_renderer_ms']['median'] for r in grouped[k]) for k in grouped]
    parts.append(f'\nABBA, 4.000/4.000 comandos respondidos. Mediana das medianas SDL: **{f(a)} → {f(b)} ms ({f(100*(a-b)/a)}% de redução)**. Renderer: **{f(ar)} → {f(br)} ms ({f(100*(ar-br)/ar)}%)**. Estes são ganhos incrementais desta sessão; não se subtrai o resultado de um log antigo de 150–160 ms.\n')
    parts.append(table(['Métrica; mediana das duas rodadas','Referência','Candidata'],[[label,*[f(median(r['sdl_first_redraw_ms'][key] for r in grouped[k])) for k in grouped]] for label,key in [('p95 SDL ms','p95'),('p99 SDL ms','p99'),('Máximo SDL por rodada ms','max')]]))
    worst=[max(r['sdl_first_redraw_ms']['max'] for r in grouped[k]) for k in grouped]
    parts.append(f'\n**Pior comando observado:** {f(worst[0])} → {f(worst[1])} ms; a candidata piorou esse máximo em {f(worst[1]-worst[0])} ms, apesar da melhora consistente de mediana/p95/p99 nas duas rodadas.\n')
    if runs[0].get('verified_selection_changes'):
        parts.append('\nAlém das respostas, foram conferidas as 4.000 mudanças de seleção esperadas no trace. Todas as versões partiram do cursor '+str(runs[0]['initial_system_cursor'])+' e conservaram a mesma ordem/contagens de sistemas (19.284 jogos) e o mesmo hash do roteiro. Esse ponto inicial atual não substitui o cenário de abertura antigo 12→13.\n')
    worst_file=V/'final-navigation-worst-events.json'
    if worst_file.exists():
        events=read(worst_file.name)
        parts.append(table(['Pior evento por rodada','Comando/tecla','Cursor','UI ms','Renderer ms','Diagnósticos ms','Worker crítico: desenho parede/CPU/fora CPU ms'],[[r['run'],str(r['command'])+'/'+r['key'],r['system_cursor'],f(r['ui_ms']),f(r['renderer_ms']),f(r['diagnostics_ms']),'/'.join(f(r[k]) for k in ('critical_draw_wall_ms','critical_draw_cpu_ms','critical_draw_off_cpu_ms'))] for r in events]))
        parts.append('\nO máximo de 230,190 ms ocorreu na troca de sistema do comando 440/Left. O worker crítico passou 184,757 ms desenhando, mas contabilizou 62,492 ms de CPU: aproximadamente 122,266 ms ficaram fora da CPU. Essa diferença pode incluir bloqueios/preempção; a causa específica não foi identificada. Diagnósticos consumiram 7,479 ms nesse evento. Não se atribui todo o máximo ao cálculo de alfa, prefetch ou governador sem evidência causal.\n')
    parts.append('\nOs máximos individuais e as regressões estão expostos na tabela. Percentis das medianas de rodadas não são percentis de uma distribuição agrupada. O primeiro controle conserva seu rótulo original final-navigation-v1-4-reference; não foi copiado ou rebatizado. As três rodadas seguintes completam ABBA na mesma sessão.\n')
    if nav.get('inter_run_gaps_s'):
        parts.append('\nIntervalos entre a última resposta de uma rodada e a primeira injeção da seguinte, em segundos: '+', '.join(f(x) for x in nav['inter_run_gaps_s'])+'. Esses intervalos não são contados como latência de comando.\n')
    stages=[('update_ns','Preparação/update ms'),('record_ns','Gravação da UI ms'),('dirty_ns','Regiões alteradas ms'),('draw_max_ms','Maior desenho de worker ms'),('copy_max_ms','Maior cópia de worker ms'),('post_ns','Manutenção posterior ms'),('fps_limit_ns','Limitador da UI ms'),('pacing_ns','Pacing do renderer ms'),('off_cpu_draw_max_ms','Maior parcela fora da CPU durante desenho ms')]
    parts.append('## Onde o tempo permanece\n\n'+table(['Etapa: mediana em quadros completos',*[r['implementation']+str(i+1) for i,r in enumerate(runs)]],[[label,*[f(r['full_stages'][key]['median']) for r in runs]] for key,label in stages]))
    parts.append('''
O caminho medido é fila SDL → parseEvent → window.update → window.render
(record dos comandos) → findParts → preparação/publicação das faixas →
executePart/executeBand nos dois workers → cópia para o framebuffer → join e
manutenção. execute_ns mede o intervalo real inteiro do renderer com cópia.
Desenho de um worker pode se sobrepor à cópia de outro: não se somam máximos
independentes para reconstruir um quadro. Medianas de etapas também não formam
necessariamente o mesmo quadro. A espera na fila SDL inclui a posição da injeção
histórica no loop, não mede isoladamente o escalonador Linux.
''')
    parts.append(table(['Rodada','Quadros desenhados','Acima de 16,7 ms','Completos'],[[r['run'],r['drawn_frames'],r['drawn_above_16_7_ms'],r['full_frames']] for r in runs]))
    parts.append(f'\nCom renderer completo em cerca de {f(br)} ms, alcançar 16,7 ms ainda exigiria cortar aproximadamente **{f(br-1000/60)} ms ({f(100*(1-(1000/60)/br))}% do restante)**. Isso explica o limite observado desta implementação; não prova um limite físico absoluto do hardware.\n')
else:parts.append('**Pendente: comparação final de quatro rodadas.**\n')
transitions=V/'final-transitions-summary.json'
if transitions.exists():
    data=read(transitions.name)['runs']
    parts.append('## Término da transição principal\n\n'+table(['Rodada','Comandos','Término observado','Mediana até terminar ms','p95','Intervalo mediano entre desenhos ms','Intervalos >16,7 ms'],[[r['run'],r['commands'],r['completed'],f(r['main_transition_end_ms']['median']),f(r['main_transition_end_ms']['p95']),f(r['redraw_intervals_in_transition_ms']['median']),r['intervals_above_16_7_ms']] for r in data]))
    parts.append('\nMede o primeiro quadro escrito com a animação principal SystemView/slot 0 inativa após uma amostra ativa, mantendo o mesmo cursor. Não inclui o fade informativo do slot 2 nem a TV. Uma nova tecla pode interromper a animação; trace apenas de redesenhos também pode omitir um estado lógico final sem alteração de pixels. Ausência de término observado não significa comando perdido. As durações acima usam um subconjunto diferente em cada versão, favorecendo intervalos longos entre comandos; não demonstram sozinhas uma alteração de duração. O cenário curto separado abaixo evita essa censura. Duração configurada e efeitos não foram alterados.\n')
quiet=V/'final-animations-v1-transitions.json'
if quiet.exists():
    data=read(quiet.name)['runs']
    parts.append('### Transições sem interrupção por nova tecla\n\n'+table(['Rodada','Comandos/términos observados','Término mediana ms','p95','Máximo','Intervalo de desenho mediana ms','Intervalos >16,7 ms'],[[r['run'],str(r['commands'])+'/'+str(r['completed']),*[f(r['main_transition_end_ms'][k]) for k in ('median','p95','max')],f(r['redraw_intervals_in_transition_ms']['median']),r['intervals_above_16_7_ms']] for r in data]))
    parts.append('\nSão 16 comandos por rodada, ABBA, com 1.800 ms de espera adicional e trace de todas as iterações. A amostra final lógica fica disponível mesmo se os pixels já estiverem estáveis e não exigirem nova cópia. A métrica é o primeiro estado lógico inativo após o ativo; a TV não foi medida. Todos os términos são exigidos pelo analisador. Esse cenário mede transição completa e não substitui os percentis de 1.000 comandos da navegação contínua.\n')
oldfile=V/'final-navigation-v1-summary.json'
if oldfile.exists():
    old=read(oldfile.name)
    parts.append('## Comparação interrompida: preservada como evidência\n\n'+table(['Rodada','SDL mediana ms','p95','p99','Máximo','Renderer completo mediana ms'],[[r['run'],*[f(r['sdl_first_redraw_ms'][k]) for k in ('median','p95','p99','max')],f(r['full_renderer_ms']['median'])] for r in old['runs']]))
    parts.append('\nAs 4.000 respostas são válidas, mas a última referência foi executada depois de uma pausa longa de acompanhamento. Intervalos entre rodadas: '+', '.join(f(x) for x in old.get('inter_run_gaps_s',[]))+' s. A referência mudou de 107,523 para 98,623 ms; o agrupamento dessa sessão interrompida daria 103,073 contra 104,576 ms e não demonstraria ganho. Por isso foi feita uma nova comparação contínua, sem ocultar esses números. Não foi atribuído o deslocamento de desempenho ao governador ou a uma causa não medida.\n')
parts.append('''
## Capas frias, concorrência e memória

Mesma implementação durante o A/B isolado de uma/duas threads de imagens,
1.000 entradas compartilhando 160 PNGs originais 512×768, três ciclos de
ida/volta/carrossel/lista/menu e repouso. Todas as 3.888 entradas foram respondidas,
com um único frontend. Esse grid tem carga diferente do carrossel histórico.
''')
covers=read('covers-final-v2-summary.json')['runs']
parts.append(table(['Rodada','Decoder','SDL mediana ms','p95','p99','Máximo','RSS pico MiB'],[[r['run'],r['texture_threads'],*[f(r['sdl_first_redraw_ms'][k]) for k in ('median','p95','p99','max')],f(r['rss_peak_mib'])] for r in covers]))
parts.append('''
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
''')
parts.append('## Repouso e controles\n')
for mode in ('idle','controls'):
    path=V/f'final-{mode}-v1-summary.json'
    if not path.exists():parts.append(f'**Pendente: {mode}.**\n');continue
    data=read(path.name)['runs']
    if mode=='idle':
        parts.append(table(['Rodada; 15 s de repouso','Respostas','SDL mediana ms','p95','p99','uinput mediana ms','CPU estática % de um core','RSS estático MiB'],[[r['run'],r['responses'],*[f(r['sdl_first_redraw_ms'][k]) for k in ('median','p95','p99')],f(r['uinput_first_redraw_ms']['median']),f(r['static_idle_cpu_percent_one_core_units']['median']),f(r['static_idle_rss_mib']['median'])] for r in data]))
        if all('first_response_renderer_ms' in r for r in data):parts.append('\n'+table(['Primeira resposta após repouso','Renderer mediana ms','Respostas parciais/total'],[[r['run'],f(r['first_response_renderer_ms']['median']),str(r['first_response_partial_frames'])+'/'+str(r['responses'])] for r in data]))
        parts.append('\nOito entradas por rodada: p99 pequeno coincide com o máximo. Repouso alterna dois sistemas adjacentes e pode responder com desenho parcial; não se compara diretamente ao percurso contínuo de quatro posições por lado. uinput inclui evdev/SDL, sem USB/radio/TV físicos.\n')
    else:parts.append(table(['Rodada','Toque breve','Avanços segurando Right/Left','Parou ao soltar'],[[r['run'],r['short_advance'],str(r['held_right_advance'])+'/'+str(r['held_left_advance']),r['release_stable']] for r in data]))
parts.append('## Abertura completa\n\nO wrapper é o fonte ativo, em pacote isolado com os mesmos assets/configuração; a injeção começa após o primeiro quadro interativo e verifica mudança de seleção. A abertura por chamada SSH não comprova o tempo de fechamento do menu Scripts físico.\n')
for mode in ('warm','cold'):
    paths=sorted((V/'target-results').glob(f'startup-full-{mode}-v1-*/startup-summary.json'))
    if len(paths)!=4:parts.append(f'**Pendente: abertura {mode} completa, {len(paths)}/4 rodadas analisadas.**\n');continue
    data=[json.loads(p.read_text()) for p in paths]
    parts.append(f'### {mode}: '+('cache aquecido' if mode=='warm' else 'primeira abertura após reboot real')+'\n\n'+table(['Rodada','Wrapper→entrada ms','Processo→entrada ms','Helper ms','Sleep fixo ms','Load systems s','Primeiro quadro interativo s','Seleção'],[[r['run'],f(r['wrapper_to_first_input_ms']),f(r['spawn_to_first_input_ms']),f(r['helper_ms']),f(r['wrapper_fixed_wait_ms']),f(r['load_systems_s']),f(r['first_interactive_s']),str(r['selection_before'])+'→'+str(r['selection_after'])] for r in data]))
    parts.append('\n'+table(['Mediana das duas rodadas','Referência','Candidata'],[[label,*[f(median(r[key] for r in data if ('candidate' in r['run'])==candidate)) for candidate in (False,True)]] for label,key in [('Wrapper→primeira entrada ms','wrapper_to_first_input_ms'),('Processo→primeira entrada ms','spawn_to_first_input_ms')]]))
    parts.append('\n'+table(['Primeira entrada após a primeira tela; mediana das duas rodadas','Referência','Candidata'],[[label,*[f(median(r[key] for r in data if ('candidate' in r['run'])==candidate)) for candidate in (False,True)]] for label,key in [('Injeção uinput→redesenho ms','uinput_ms'),('Fila SDL→redesenho ms','sdl_input_ms'),('Primeiro quadro interativo desde o processo s','first_interactive_s'),('LoadConfig s','load_systems_s')]]))
    parts.append('\nA antecipação da primeira tela foi conferida junto com a primeira entrada efetivamente processada. O custo dessa navegação inicial está incluído no tempo wrapper→entrada e também aparece isolado na tabela; os ganhos não são somados. São duas execuções por versão, sem garantia de p99 de abertura.\n')
    if mode=='cold':parts.append('\nNovo boot_id foi exigido por rodada; nenhum hash/pré-leitura do ELF antes de iniciar o processo. Os serviços originais permaneceram ativos e receberam o mesmo intervalo de estabilização. O reboot não é drop_caches.\n')
parts.append('''
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
''')
core=V/'target-results/final-core-v1/lifecycle-summary.json'
if core.exists():
    result=json.loads(core.read_text())
    parts.append('\nRetorno de core, passagem de qualidade: '+str(result['quality_checked_frames'])+' quadros conferidos, '+str(result['quality_wrong_frames'])+' divergências. A conexão falhou antes de enviar MENU; NES e PID foram conferidos antes de recuperar o retorno. O core permaneceu ativo por cerca de 157 s, em vez dos oito planejados. Esse intervalo e a verificação dupla não são tratados como desempenho normal. A primeira contagem de inputs não demonstrava ações úteis; isso foi corrigido na passagem seguinte por verificação explícita do estado/seleção.\n')
speed=V/'target-results/final-core-speed-v2/lifecycle-summary.json'
if speed.exists():
    r=json.loads(speed.read_text())
    parts.append('### Retorno normal com o programa original de teste\n\n'+table(['Etapa medida no MiSTer','Tempo ms'],[['Espera fixa após MENU observado pelo launcher',f(r['return_fixed_wait_ms'])],['Ativação do framebuffer pelo helper',f(r['return_helper_ms'])],['Helper concluído→primeiro desenho',f(r['helper_complete_to_first_draw_ms'])],['MENU observado pelo launcher→primeiro desenho',f(r['menu_observed_to_first_draw_ms'])]]))
    parts.append('\nVerificação dupla, PNG, slow dumps e profiler desligados; mesmo binário candidato em 1080p. O catálogo deste teste é mínimo, com um programa NES original sem save RAM: não é uma medição de ganho antes/depois no catálogo real. Houve uma única abertura de core. Após voltar, cinco probes confirmaram carrossel ativo e seleção 0→1→0 com as setas.\n\nO roteiro atrasou deliberadamente esses controles e esperou uma condição de repouso que o grid animado não atingia. O acompanhamento expirou antes do roteiro, mas a execução autônoma terminou e foi coletada com os estados esperados. O primeiro desenho acima foi medido antes dessa espera; o timestamp tardio dos controles não estima a menor prontidão de entrada. O roteiro de reprodução foi corrigido para usar a espera de segurança e probes de seleção, sem exigir que um grid animado pare de redesenhar. A menor prontidão de entrada após retorno permanece não medida.\n\nA tentativa final-core-speed-v1 foi invalidada: um Return enviado antes de confirmar a tela reabriu o próprio programa de teste. O retorno foi recuperado após conferir NES/PID, sem outro frontend; seus números não entram na aceitação. Os artefatos foram preservados.\n')
else:parts.append('\n**Pendente: retorno normal de core com controles confirmados.**\n')
layerfile=V/'profile-layer-costs.json'
if layerfile.exists():
    layers=read(layerfile.name)
    parts.append('## Camadas: perfil detalhado separado\n\n'+table(['Grupo por dimensões/vértices','Amostras comando/worker','Visitas em faixas','Soma de parede dos workers ms','Participação nessa soma %'],[[r['label'],r['command_worker_samples'],r['band_visits'],f(r['sum_worker_wall_ms']),f(r['share_sum_worker_wall_percent'])] for r in layers['groups']]))
    parts.append('\nEsse perfil anterior amostra quadros selecionados. Imagens grandes somam aproximadamente 60,9%; o grupo de atlas/texto/geometria, 12,2%. A soma dos dois workers não é o tempo real do quadro, pode conter pausas fora da CPU e inclui custo dos marcadores detalhados. Primeiro alfa não prova opacidade da textura. Esses grupos permitem priorizar composição e imagens, mas não atribuir um custo semântico exato a cada fundo/capa/overlay sem mapear o asset. Não foram somados aos tempos dos testes rápidos.\n')
parts.append('''
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
''')
deploy=V/'deployment-final-20261115.json'
parts.append('\n## Instalação\n\n'+('Registro verificado: `'+deploy.name+'`.\n\n```json\n'+deploy.read_text()+'\n```\n' if deploy.exists() else '**Ainda não instalada: aguarda a conclusão e revisão da matriz.**\n'))
verification=V/'installed-final-verification.json'
if verification.exists():
    live=read(verification.name)
    parts.append('\nVerificação da execução normal: PID '+str(live['pid'])+', um frontend e um wrapper, primeiro quadro interativo confirmado, 1920×1080×32 bits, Carbon `'+live['theme_settings'].get('ThemeSet','')+'`, STRIP=8/PLD=256/FPS=60. Trace de quadros/texturas, verificação dupla, PNG/roteiro e profiler desligados. SHA-256 instalado confere com a candidata medida. OOM deste boot: '+str(live['oom_kill'])+'; políticas CPUFreq: '+str(live['cpufreq_policies'])+'. A cópia anterior e seus hashes estão no backup acima.\n')
normal=V/'normal-runtime-sample.json'
if normal.exists():
    r=read(normal.name)
    parts.append('\nObservação da execução instalada sem roteiro/trace, por '+f(r['duration_s'])+' s: RSS '+f(r['rss_min_mib'])+'–'+f(r['rss_max_mib'])+' MiB, VmHWM '+f(r['hwm_mib'])+' MiB e CPU '+f(r['cpu_percent_one_core_units'])+'% de um core. O usuário experimentou o controle nesse período; não se trata de repouso controlado, comparação antes/depois de CPU ou prova de estabilidade ilimitada. O benchmark de repouso acima usava roteiro ativo e não deve ser confundido com essa observação normal.\n')
physical=V/'physical-validation.json'
if physical.exists():
    r=read(physical.name)
    parts.append('\nValidação física informada pelo usuário em 15/11: **controle funcionando normalmente na TV**. Áudio do frontend: **sem som, como antes**, sem regressão relatada. Isso confirma funcionamento observado; não mede controle→fótons. O áudio de jogos não foi validado pelo programa de vídeo silencioso.\n')
D.mkdir(parents=True,exist_ok=True);(D/'FECHAMENTO.md').write_text('\n\n'.join(parts),encoding='utf-8')
print(D/'FECHAMENTO.md')
