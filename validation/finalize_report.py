import pathlib,json,re,shutil
v=pathlib.Path(__file__).resolve().parent
out=v.parent/'es-mister/audit/performance-1080p'
data={n:json.loads((v/'target-results'/n/'summary.json').read_text()) for n in ('sustained-control','sustained-fast')}
a,b=data.values()
table='''<!-- MEASUREMENTS BEGIN -->
## Comparação sustentada

| Medida (ms) | Controle | Otimizada | Redução |
|---|---:|---:|---:|
'''
for key,stat,label in [('input_sdl_event_ms','median','Fila SDL → redesenho, mediana'),('input_sdl_event_ms','p95_sample','Fila SDL → redesenho, p95'),('input_sdl_event_ms','p99','Fila SDL → redesenho, p99'),('input_sdl_event_ms','max','Fila SDL → redesenho, máximo observado'),('input_dispatch_ms','median','Despacho → redesenho, mediana'),('input_dispatch_ms','p99','Despacho → redesenho, p99')]:
    x,y=a[key][stat],b[key][stat]
    table+=f'| {label} | {x:.3f} | {y:.3f} | {(1-y/x)*100:.1f}% |\n'
table+='\nCada versão respondeu exatamente 1.000 comandos. O controle durou '
table+=f"{a['memory']['duration_seconds']:.1f} s e a otimizada {b['memory']['duration_seconds']:.1f} s, incluindo inicialização e encerramento. "
table+='A duração total também inclui esperas artificiais do script; não é uma medida de throughput.\n\n'
table+='| Etapa do redesenho completo, mediana (ms) | Controle | Otimizada |\n|---|---:|---:|\n'
for key,label in [('ui_ms','Preparação da UI'),('dirty_ms','Regiões sujas'),('execute_ms','Execução incluindo cópia'),('draw_max_ms','Desenho do worker mais lento'),('copy_max_ms','Cópia do worker mais lento'),('pacing_ms','Espera do limitador')]:
    table+=f"| {label} | {a['full_navigation'][key]['median']:.3f} | {b['full_navigation'][key]['median']:.3f} |\n"
table+='\nAs distribuições de quadros diferem porque o limitador corrigido permite mais passos de animação '
table+='e o culling permite atualizações parciais. A comparação principal é a resposta aos mesmos inputs. '
table+='**Não se atingiu 60 FPS nos redesenhos completos do Carbon em 1080p.**\n\n'
table+='Memória residente máxima durante o teste instrumentado: '
table+=f"{a['memory']['peak_rss_kib']/1024:.2f} MiB no controle e {b['memory']['peak_rss_kib']/1024:.2f} MiB na otimizada. "
table+='Depois de preencher o buffer de tracing e antes do encerramento, as medianas de janelas de 100 s foram '
table+=f"{b['memory']['steady_early_rss_mib']['median']:.3f} e {b['memory']['steady_late_rss_mib']['median']:.3f} MiB na otimizada. "
table+='Não houve crescimento relevante nessa janela; isso não prova ausência de vazamentos em todas as telas. '
table+='O tracing reserva e ocupa memória progressivamente; os números não são o consumo mínimo da execução normal.\n\n'
quality=v/'target-results/quality-final/stats.txt'
if quality.exists():
    table+='Na validação separada de qualidade, **0 de 91 quadros verificados divergiram** entre renderização parcial e referência completa. '
    table+='Carrossel, lista NES, mudança de seleção e menu foram capturados em 1920×1080 e inspecionados visualmente. '
    table+='Esse modo redesenha a referência e salva PNGs; seus tempos não são usados como benchmark.\n\n'
one=v/'target-results/threads-one/summary.json'
if one.exists():
    c=json.loads(one.read_text())
    table+=f"Uma thread teve mediana interna de {c['input_dispatch_ms']['median']:.3f} ms em 24 comandos, contra {b['input_dispatch_ms']['median']:.3f} ms com duas threads no teste longo. "
    table+='As amostras têm tamanhos diferentes, mas não apareceu razão para trocar o padrão de duas threads.\n\n'
startup=[]
for label in ('startup-control-final','startup-fast-final'):
    p=v/'target-results'/label/'startup.log'
    if not p.exists():continue
    text=p.read_text()
    ready=re.findall(r'^\s*([\d.]+) \[\d+\] ready$',text,re.M)
    action=re.findall(r'^\s*([\d.]+) \[\d+\] first press answered:',text,re.M)
    if ready and action:startup.append((label,float(ready[-1]),float(action[-1])))
if startup:
    table+='Inicialização de processo novo com cache do sistema já aquecido, uma execução de cada:\n\n'
    table+='| Versão | Marca ready (s) | Primeiro comando respondido (s) |\n|---|---:|---:|\n'
    for label,ready,action in startup: table+=f'| {label} | {ready:.3f} | {action:.3f} |\n'
    table+='\nO script aguarda 9 s antes da tecla; o primeiro comando confirma funcionamento nesse instante e '
    table+='é um limite superior, não uma busca do primeiro milissegundo interativo. Os valores excluem a espera '
    table+='inicial do wrapper. Não houve reboot/limpeza global de cache para medir cold boot e não se conclui '
    table+='ganho estatístico de startup com uma amostra.\n\n'
table+='<!-- MEASUREMENTS END -->'
path=out/'RESULTADOS.md';text=path.read_text()
text=text.replace('<!-- MEASUREMENTS -->',table) if '<!-- MEASUREMENTS -->' in text else re.sub(r'<!-- MEASUREMENTS BEGIN -->.*?<!-- MEASUREMENTS END -->',lambda _:table,text,flags=re.S)
path.write_text(text)
e=out/'evidencias-etapa2';e.mkdir(exist_ok=True)
for name in ('stage2-checks.log','regressions-stage2.log','settings-stage2.log','renderer-stage2.log','http-server-stage2.log','thread-pool-stage2.log','copy-file-stage2.log','similarity-stage2.log','theme-stage2.log','helper-target-validation.log','raster-final-native.log','raster-target-results.log','finish-target-checks.log','finish-target-checks-retry.log','cross-build-performance.log','deployment.log','deployment-verification.json'):
    if (v/name).exists():shutil.copy2(v/name,e/name)
for name in ('installed-control','profile','transparent','transparent-off','alpha','sustained-control','sustained-fast','quality-final','threads-one','startup-control-final','startup-fast-final'):
    if (v/'target-results'/(name+'.tar.gz')).exists():shutil.copy2(v/'target-results'/(name+'.tar.gz'),e)
    p=v/'target-results'/name/'summary.json'
    if p.exists():shutil.copy2(p,e/(name+'-summary.json'))
for name in ('target_run.py','trace_analyze.py','raster_build.py','target_raster.py','cross_build.sh','cross_cache.sh','cross_sources.py','cross_prepare.sh','build_checks.py','final_checks.py','quality-script.txt','startup-script.txt','arm_similarity.sh','finish_target_checks.py'):
    if (v/name).exists():shutil.copy2(v/name,e/name)
shots=v/'target-results/quality-final'
for name in ('carousel.png','games.png','menu.png'):
    if (shots/name).exists():shutil.copy2(shots/name,e/name)
(e/'runtime-manifest.json').write_text((v/'update-1080p/manifest.json').read_text())
print('Updated',path)
