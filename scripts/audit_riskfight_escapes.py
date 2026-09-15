import argparse
import gzip
import hashlib
import json
import subprocess
from collections import Counter
from pathlib import Path

from rescore_riskfight_sweep import ladder_args, read_config


def summarize(events):
    actors = {}
    for actor in (0, 1):
        rows = [row for row in events if row['actor'] == actor]
        actors[str(actor)] = {
            'teleports': len(rows),
            'both_teleported': sum(row['other_escaped'] for row in rows),
            'outcomes': dict(Counter(row['outcome'] for row in rows)),
            'triple_opponent_weapons': dict(Counter(row['opponent_weapon'] for row in rows if row['healing'] == 3)),
            'healing': dict(Counter(row['healing'] for row in rows)),
            'no_boost': sum(row['no_boost'] for row in rows),
            'both_no_special_truth': sum(row['both_no_special'] for row in rows),
            'tick_zero': sum(row['tick'] == 0 for row in rows),
            'triple_full_hp': sum(row['healing'] == 3 and row['hp'] >= 99 for row in rows),
            'triple_no_boost': sum(row['healing'] == 3 and row['no_boost'] for row in rows),
            'triple_both_no_special_truth': sum(row['healing'] == 3 and row['both_no_special'] for row in rows),
            'triple_food_delay': sum(row['healing'] == 3 and row['food_timer'] > 0 for row in rows),
            'triple_full_starting_food': sum(row['marlins'] == 8 and row['brew_doses'] == 8 and
                                            row['halibut'] == 4 and row['pie_bites'] == 6 for row in rows),
        }
    return actors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--curriculum-root', required=True, type=Path)
    parser.add_argument('--winner-config', required=True, type=Path)
    parser.add_argument('--winner-checkpoint', required=True, type=Path)
    args = parser.parse_args()
    models = json.loads((args.curriculum_root / 'results.json').read_text())
    for model in models:
        model['config'] = str(args.curriculum_root / 'logs/osrs_riskfight' /
                              (Path(model['checkpoint']).parent.name + '.ini'))
    models.append({'condition': 'original_winner', 'training_seed': -1,
                   'checkpoint': str(args.winner_checkpoint), 'config': str(args.winner_config)})
    args.root.mkdir()
    (args.root / 'manifest.json').write_text(json.dumps({
        'git_sha': subprocess.check_output(['git','rev-parse','HEAD'], cwd=args.repo, text=True).strip(),
        'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'models': models, 'evaluation_seeds': [1009,2027,3037],
        'healing_tiers': ['none','other healing without specified double','marlin plus brew or halibut','marlin plus brew plus halibut'],
        'special_energy_source': 'simulator truth, not policy observations',
        'snapshot': 'successful teleport inventory click, after earlier clicks in that tick',
    }, indent=2))
    status = args.root / 'status.json'
    status.write_text('{"status":"running"}')
    results = []
    try:
        for index, model in enumerate(models):
            config = read_config(model['config'])
            for seed in (1009,2027,3037):
                (args.root / 'progress.json').write_text(json.dumps({'model':index,'condition':model['condition'],'seed':seed}))
                config['base']['seed'] = str(seed)
                command = ladder_args(args.binary, Path(model['checkpoint']), config, '0,1,2,4,5,6,8,7')
                command.append('--env.escape_trace=1')
                process = subprocess.Popen(command, cwd=args.repo, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                events, bots = [], []
                with gzip.open(args.root / f'model-{index}-seed-{seed}.log.gz', 'wt') as log:
                    for line in process.stdout:
                        log.write(line)
                        if line.startswith('RF_ESCAPE '):
                            events.append(json.loads(line.removeprefix('RF_ESCAPE ')))
                        elif line.startswith('LADDER_EVAL bot='):
                            fields = dict(item.split('=',1) for item in line.split()[1:])
                            row = {key: int(value) if key in ('bot','games') else float(value) for key,value in fields.items()}
                            row['escapes'] = summarize(events)
                            bots.append(row)
                            events = []
                process.wait()
                assert process.returncode == 0, (index,seed,process.returncode)
                assert len(bots) == 8 and not events
                results.append({'condition':model['condition'],'training_seed':model['training_seed'],
                                'evaluation_seed':seed,'bots':bots})
                (args.root / 'results.json').write_text(json.dumps(results,indent=2))
                print(json.dumps({'completed_evaluations':len(results),'model':index,'seed':seed}),flush=True)
        status.write_text('{"status":"complete"}')
    finally:
        if json.loads(status.read_text())['status'] == 'running':
            status.write_text('{"status":"failed"}')


if __name__ == '__main__':
    main()
