"""Paired official Requiem runs with reversible proxy/config replacement; no gameplay FPS claim."""

import argparse
import csv
import ctypes
import json
import math
import statistics
import subprocess
from pathlib import Path

from PIL import Image, ImageChops
import benchmark_nr_baseline as bench


ROOT = Path(__file__).resolve().parents[1]
METRICS = ['nr_gpu_ms', 'ngx_evaluation_gpu_ms', 'frame_wall_ms']


def measure(args, height, repeat, variant, directory, proxy):
    output = args.output / f'{height}-{repeat + 1}-{variant}'
    output.mkdir()
    (directory / 'version.dll').write_bytes(proxy.read_bytes())
    runtime_log = directory / 'nrfusion.log'
    offset = runtime_log.stat().st_size if runtime_log.exists() else 0
    width = {1080: 1920, 1440: 2560, 2160: 3840}[height]
    command = [str(directory / 'RequiemGame.exe'), '--require-nrfusion-proxy', '--require-nr',
               '--deterministic-motion', '--width', str(width), '--height', str(height),
               '--frames', str(args.frames), '--warmup', str(args.warmup),
               '--csv', str(output / 'frames.csv')]
    if args.typeless:
        command.append('--typeless-guides')
    if repeat == 0:
        command += ['--capture', str(output / 'capture.ppm')]
    print(f'RUN {height} pair={repeat + 1} {variant}', flush=True)
    with (output / 'run.log').open('wb') as log:
        finished = subprocess.run(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT, timeout=240)
    if finished.returncode:
        raise RuntimeError(f'{output}: process exit {finished.returncode}')
    with runtime_log.open('rb') as log:
        log.seek(offset)
        runtime = log.read().decode('utf-8', errors='replace')
    (output / 'runtime.log').write_text(runtime, encoding='utf-8')
    if 'Captured game NGX feature=' in runtime:
        raise RuntimeError('Double NR risk: game hook also captured proxy feature')
    with (output / 'frames.csv').open(newline='') as source:
        rows = list(csv.DictReader(source))
    if [int(row['frame']) for row in rows] != list(range(args.warmup, args.frames)):
        raise RuntimeError('Missing or duplicated frame samples')
    for row in rows:
        actual = tuple(int(row[field]) for field in
                       ['input_width', 'input_height', 'output_width', 'output_height'])
        if actual != (width * 2 // 3, height * 2 // 3, width, height):
            raise RuntimeError('Unexpected resolution')
        if int(row['nr_passes']) != 1 or int(row['nr_successes']) != 1:
            raise RuntimeError('NR did not execute exactly once successfully')
    copies = sorted({int(row['guide_copies']) for row in rows})
    expected = 2 if args.typeless and variant == 'reference' else 0
    if copies != [expected]:
        raise RuntimeError(f'Expected {expected} guide copies, observed {copies}')
    metrics = {}
    for metric in METRICS:
        samples = [float(row[metric]) for row in rows]
        if any(not math.isfinite(value) or value <= 0 for value in samples):
            raise RuntimeError(f'Invalid {metric} sample')
        metrics[metric] = {f'p{int(fraction * 100)}': bench.percentile(samples, fraction)
                           for fraction in [.5, .95, .99]}
    print(f'OK NR p50={metrics["nr_gpu_ms"]["p50"]:.4f}', flush=True)
    return {'height': height, 'repeat': repeat + 1, 'variant': variant, 'command': command,
            'metrics': metrics, 'copies': copies,
            'copy_bytes': sorted({int(row['guide_copy_bytes']) for row in rows})}


def summarize(args, runs):
    summaries = {}
    for height in args.heights:
        summary = {}
        for metric in METRICS:
            summary[metric] = {}
            for percentile in ['p50', 'p95', 'p99']:
                before = [run['metrics'][metric][percentile] for run in runs
                          if run['height'] == height and run['variant'] == 'reference']
                after = [run['metrics'][metric][percentile] for run in runs
                         if run['height'] == height and run['variant'] == 'candidate']
                changes = [100 * (new / old - 1) for old, new in zip(before, after)]
                summary[metric][percentile] = {
                    'before_ms': statistics.median(before), 'after_ms': statistics.median(after),
                    'paired_delta_percent': changes, 'median_delta_percent': statistics.median(changes)}
        captures = [args.output / f'{height}-1-{variant}/capture.ppm'
                    for variant in ['reference', 'candidate']]
        with Image.open(captures[0]) as before, Image.open(captures[1]) as after:
            exact = ImageChops.difference(before, after).getbbox() is None
        summary['captured_rgb_exact'] = exact
        summary['capture_sha256'] = [bench.sha256(path) for path in captures]
        if not exact:
            raise RuntimeError(f'Capture mismatch at {height}')
        summaries[str(height)] = summary
    return summaries


def benchmark(args):
    directory = ROOT / 'dist/RequiemGame'
    protected = ['version.dll', 'nrfusion.ini', 'nrfusion_advanced.ini']
    originals = {name: (directory / name).read_bytes() if (directory / name).exists() else None
                 for name in protected}
    power = (ctypes.c_ubyte * 12)()
    if not ctypes.windll.kernel32.GetSystemPowerStatus(power) or power[0] != 1:
        raise RuntimeError('Benchmark requires AC power')
    if not args.reference_proxy.is_file() or not args.candidate_proxy.is_file():
        raise RuntimeError('Both proxy binaries must exist')
    args.output.mkdir(parents=True)
    payloads = ['RequiemGame.exe', 'nvngx_dlss.dll', 'nvngx_dlssg.dll',
                'NRFusion/internal/nvngx_dlssnr.dll', 'NRFusion/internal/nvngx.dll_dlssnr.dll',
                'assets/nvidia-dlss5-requiem-off.jpeg', 'assets/nvidia-dlss5-requiem-on.jpeg']
    results = {'reference_commit': args.reference_commit, 'candidate_commit': args.candidate_commit,
               'frames': args.frames, 'warmup': args.warmup, 'repeats': args.repeats,
               'gpu': subprocess.check_output(['nvidia-smi', '--query-gpu=name,uuid,driver_version',
                                              '--format=csv,noheader'], text=True).strip(),
               'settings': bench.SETTINGS, 'typeless': args.typeless,
               'proxy_sha256': {'reference': bench.sha256(args.reference_proxy),
                                'candidate': bench.sha256(args.candidate_proxy)},
               'payload_hashes': {name: bench.sha256(directory / name) for name in payloads}, 'runs': []}
    telemetry_file = (args.output / 'telemetry.csv').open('wb')
    telemetry = subprocess.Popen(['nvidia-smi', '--query-gpu=timestamp,pstate,temperature.gpu,'
        'clocks.current.graphics,clocks.current.memory,power.draw,utilization.gpu', '--format=csv', '-l', '1'],
        stdout=telemetry_file, stderr=subprocess.STDOUT)
    try:
        bench.configure(directory, True)
        configs = {name: (directory / name).read_bytes() for name in protected[1:]}
        for repeat in range(args.repeats):
            heights = args.heights if repeat % 2 == 0 else list(reversed(args.heights))
            for height in heights:
                order = ['reference', 'candidate'] if repeat % 2 == 0 else ['candidate', 'reference']
                for variant in order:
                    proxy = args.reference_proxy if variant == 'reference' else args.candidate_proxy
                    results['runs'].append(measure(args, height, repeat, variant, directory, proxy))
                    if any((directory / name).read_bytes() != content for name, content in configs.items()):
                        raise RuntimeError('Configuration changed during measurement')
        results['summary'] = summarize(args, results['runs'])
    finally:
        for name, content in originals.items():
            if content is None:
                (directory / name).unlink(missing_ok=True)
            else:
                (directory / name).write_bytes(content)
        telemetry.terminate()
        telemetry.wait(timeout=10)
        telemetry_file.close()
        (args.output / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results['summary'], indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-proxy', type=Path, required=True)
    parser.add_argument('--candidate-proxy', type=Path, required=True)
    parser.add_argument('--reference-commit', required=True)
    parser.add_argument('--candidate-commit', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--typeless', action='store_true')
    parser.add_argument('--heights', type=int, choices=[1080, 1440, 2160], nargs='+', default=[1080, 1440, 2160])
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--warmup', type=int, default=150)
    parser.add_argument('--repeats', type=int, default=5)
    arguments = parser.parse_args()
    arguments.output = arguments.output.resolve()
    arguments.reference_proxy = arguments.reference_proxy.resolve()
    arguments.candidate_proxy = arguments.candidate_proxy.resolve()
    if arguments.warmup < 1 or arguments.frames <= arguments.warmup or arguments.repeats < 1:
        parser.error('Require frames > warmup >= 1 and repeats >= 1')
    benchmark(arguments)
