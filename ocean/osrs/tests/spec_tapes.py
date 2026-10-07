import json,csv,collections,sys
SP='/private/tmp/claude-501/-Users-valtterivalo-Projects-storm-light/d0f7358b-e79e-42d9-be57-13e0474fafc9/scratchpad/spectator/out/'
R='/private/tmp/claude-501/-Users-valtterivalo-Projects-storm-light/d0f7358b-e79e-42d9-be57-13e0474fafc9/scratchpad/rollout/'
OUT=R+'tapes_spec/'
tmpl=None
for l in open(R+'tapes_all/20261006T225213._0_Wlip.tape'):
    if l.startswith('T '): tmpl=l.split(); break
TW=36;WORN=tmpl[36:49]; BAG=tmpl[49:77]
VW,MAUL=27690,24225
SUPPORTED={VW:'27690',24225:'24225'}
duels={}
for l in open(SP+'duels.jsonl'):
    d=json.loads(l); duels[d['id']]=d
rows=collections.defaultdict(list)
for r in csv.DictReader(open(SP+'ticks.csv')): rows[r['duel']].append(r)
def num(x,default=None):
    return default if x=='' or x is None else float(x)
def build(d,side,other,idx):
    rs=sorted((r for r in rows[d['id']] if d['start']<=int(r['tick'])<=d['end']),key=lambda r:int(r['tick']))
    if len(rs)<3: return None
    lo,hi=int(rs[0]['tick']),int(rs[-1]['tick'])
    if hi-lo+1!=len(rs): return None
    # initial weapon
    wep0=next((int(r[side+'_wep']) for r in rs if r[side+'_wep']),VW)
    worn=list(WORN); bag=list(BAG)
    wslot=2; mslot=bag.index('24225')
    if wep0==MAUL: worn[wslot]='24225'; bag[mslot]='27690'
    cur_wep=wep0 if wep0 in (VW,MAUL) else VW
    energy={'x':100.0,'y':100.0}; anchor={'x':None,'y':None}
    t_lines=[];h_lines=[];a_lines=[];m_lines=[]
    last_bar_hit=lo-1; meals_since=0; ybar=30; xhp=99; veng_x=0;cool_x=0;veng_y=0;cool_y=0
    last_swing_x=(-100,0); last_swing_y=(-100,0)
    kit=collections.Counter()
    for r in rs:
        t=int(r['tick'])
        x=lambda k:r[side+'_'+k]; y=lambda k:r[other+'_'+k]
        # hp of x
        lo_,hi_=num(x('hp_lo')),num(x('hp_hi'))
        if lo_ is not None and hi_ is not None: xhp=int(round((lo_+hi_)/2))
        yb=num(y('bar'))
        if yb is not None: ybar=int(yb)
        for s,key in (('x',side),('y',other)):
            spec=r[key+'_spec']
            if spec and spec!='-':
                energy[s]=max(0,energy[s]-50); anchor[s]=t if anchor[s] is None else anchor[s]
            if anchor[s] is not None and t!=anchor[s] and (t-anchor[s])%50==0:
                energy[s]=min(100,energy[s]+10)
                if energy[s]==100: anchor[s]=None
        # anims
        for key,tag in ((side,'U'),(other,'O')):
            for a in (r[key+'_anims'] or '').split('|'):
                if a.strip().isdigit():
                    a_lines.append(f'A {t} {tag} {a.strip()}')
                    if tag=='O': kit[int(a)]+=1
                    if int(a) in (390,1658,11275,7045,426,10172,7514,7644,1062,4177,424,440,1060,860,386):
                        if tag=='U': last_swing_x=(t,4)
                        else: last_swing_y=(t,4)
        # hits
        xd=int(num(x('dmg'),0)); yd=int(num(y('dmg'),0))
        xn=int(num(x('nhit'),0)); yn=int(num(y('nhit'),0))
        if xd>0: h_lines.append(f'H {t} U {xd} 16 A')
        elif xn>0: h_lines.append(f'H {t} U 0 12 B')
        if yd>0: h_lines.append(f'H {t} O {yd} 16 A'); last_bar_hit=t; meals_since=0
        elif yn>0: h_lines.append(f'H {t} O 0 12 B')
        if int(num(y('eat829'),0)): meals_since+=1
        # actions of x
        if int(num(x('att'),0)): m_lines.append(f'M {t} attack -1 -1')
        wep=int(num(x('wep'),0)) if x('wep') else cur_wep
        want=wep if wep in (VW,MAUL) else cur_wep
        if want!=cur_wep:
            m_lines.append(f'M {t} click {mslot} {want}'); cur_wep=want
        spec=x('spec')
        if spec and spec!='-':
            m_lines.append(f'M {t} spec -1 -1'); m_lines.append(f'M {t} attack -1 -1')
        if int(num(x('eat829'),0)) or x('meal') in ('eat','drink'):
            heal=num(x('meal_heal'),22)
            slot,item=(0,13441) if heal>=18 or x('meal')=='eat' else (1,3144)
            m_lines.append(f'M {t} click {slot} {item}')
        if int(num(x('veng_cast'),0)): m_lines.append(f'M {t} veng -1 -1'); veng_x,cool_x=1,50
        if int(num(x('veng_proc'),0)): veng_x=0
        if int(num(y('veng_cast'),0)): veng_y,cool_y=1,50
        if int(num(y('veng_proc'),0)): veng_y=0
        ints=[t,xhp,118,118,118,99,89,3172,3509,3172,3508,int(energy['x']),0,veng_x,cool_x,31,0,ybar,30,last_bar_hit,meals_since,int(energy['y']),0,veng_y,cool_y,last_swing_y[0],last_swing_y[1],last_swing_x[0],last_swing_x[1],0,ybar,30]
        t_lines.append(' '.join(['T']+[str(v) for v in ints]+['?','NONE','1']+worn+bag))
        cool_x=max(0,cool_x-1); cool_y=max(0,cool_y-1)
    name=f"{d['id'].replace('-','_')}_{side}"
    head=[f"FIGHT {idx} {d['session']} {name} {lo} {hi}","KIT "+' '.join(f'{a}:{c}' for a,c in sorted(kit.items()))]
    open(OUT+name+'.tape','w').write('\n'.join(head+m_lines+a_lines+h_lines+t_lines)+'\n')
    return name
n=0;built=0;meta=[]
for did,d in duels.items():
    if d['a_dh'] or d['b_dh'] or d['ticks']<4: continue
    for side,other in (('a','b'),('b','a')):
        nm=build(d,side,other,n); n+=1
        if nm:
            built+=1
            meta.append((nm,d['ticks'],d[other+'_out']=='died',d[side+'_out']=='died'))
open(R+'spec/spec_meta.tsv','w').write('\n'.join('\t'.join(map(str,m)) for m in meta)+'\n')
print(n,built,sum(m[2] for m in meta),sum(m[3] for m in meta))
