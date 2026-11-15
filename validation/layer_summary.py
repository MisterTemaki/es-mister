"""Dimension-based proxies from an existing detailed trace; never runs the target."""
import collections,csv,json,pathlib,statistics
V=pathlib.Path(__file__).resolve().parent
p=V/'target-results/stage3-base-profile/trace.csv.commands.csv'
groups=collections.defaultdict(list)
for raw in csv.DictReader(p.open()):
    r={k:int(v) for k,v in raw.items()}
    w,h,alpha,vertices=(r[k] for k in ('tex_w','tex_h','first_alpha','vertices'))
    if vertices>4:label='atlas/texto ou geometria com mais de quatro vertices'
    elif w*h>=1280*720:label='imagem grande, primeiro alfa '+('parcial' if 0<alpha<255 else str(alpha))
    elif not w or not h:label='retangulo sem textura, primeiro alfa '+('parcial' if 0<alpha<255 else str(alpha))
    else:label='imagem menor, primeiro alfa '+('parcial' if 0<alpha<255 else str(alpha))
    groups[label].append(r)
total=sum(r['ns'] for values in groups.values() for r in values)
report=[]
for label,rows in groups.items():
    pairs=collections.defaultdict(int)
    for r in rows:pairs[r['frame'],r['worker']]+=r['ns']
    summed=sum(r['ns'] for r in rows)
    report.append(dict(label=label,command_worker_samples=len(rows),band_visits=sum(r['calls'] for r in rows),
        sum_worker_wall_ms=round(summed/1e6,3),share_sum_worker_wall_percent=round(100*summed/total,3),
        median_group_per_sampled_frame_worker_ms=round(statistics.median(pairs.values())/1e6,3)))
report.sort(key=lambda r:-r['sum_worker_wall_ms'])
data=dict(source=str(p.relative_to(V)),groups=report,notes=[
    'Detailed profile is separate from speed runs and only samples selected frames.',
    'Sum across both workers is not frame wall time and can include off-CPU intervals.',
    'First vertex alpha does not prove texture opacity, final blend coverage, or an asset name.',
    'Dimension/vertex groups are layer proxies, not an exact background/cover/text semantic mapping.'])
(V/'profile-layer-costs.json').write_text(json.dumps(data,indent=2,ensure_ascii=False))
print(json.dumps(data,ensure_ascii=False))
