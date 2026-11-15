import pathlib,re,subprocess,collections
v=pathlib.Path(__file__).resolve().parent
text=(v/'target-results/stage3-startup-cpu-profile/cpu-profile.txt').read_text()
part=text.split('all threads, where:\n')[1].split('main thread, where:')[0]
samples=[]
for line in part.splitlines():
    m=re.search(r'(0x[0-9a-f]+)\s+(\d+)\s+([\d.]+)%',line)
    if m:samples.append((m[1],int(m[2]),float(m[3])))
p=subprocess.run(['arm-linux-gnueabihf-addr2line','-e',str(v/'arm/emulationstation-performance'),'-f','-C',*[s[0] for s in samples]],stdout=subprocess.PIPE,text=True,check=True)
names=p.stdout.splitlines();totals=collections.Counter()
for i,(addr,count,pct) in enumerate(samples):
    name=names[i*2];location=names[i*2+1];totals[name]+=count
    print(f'{pct:5.1f}% {count:4d} {addr} {name} {location}')
print('GROUPED TOP FUNCTIONS')
for name,count in totals.most_common(20):print(count,name)
