"""Measure the official Requiem direct-proxy NR path with fixed inputs and GPU timestamps."""

import argparse
import csv
import ctypes
import hashlib
import json
import math
import platform
import statistics
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = {
    'scene': 'deterministic pan over NVIDIA OFF JPEG; synthetic depth/motion',
    'route': 'Requiem -> version.dll NGX exports -> D3D12NrExecutor -> driver SR',
    'precision': 'fp8', 'nr_scale': 1.0, 'nr_passes': 1, 'nr_placement': 'before_sr',
    'style': 'default', 'mfg': 'off', 'overlay': 'closed',
    'timing_scope': 'nr_gpu_ms includes NR preparation, model and composition',
    'frame_wall_scope': 'serialized testbed loop, Present and per-frame GPU fence; not gameplay FPS',
}


def sha256(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False), encoding='utf-8')


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize(values):
    median = statistics.median(values)
    return {'p50': median, 'p95': percentile(values, .95), 'mean': statistics.mean(values),
            'min': min(values), 'max': max(values), 'samples': len(values)}


def save_capture(path):
    from PIL import Image, ImageStat
    with Image.open(path) as image:
        deviations = ImageStat.Stat(image).stddev
        if max(deviations) < 1:
            raise RuntimeError(f'Flat/blank output: {path}')
        image.save(path.with_suffix('.png'))
        return {'sha256': sha256(path), 'rgb_stddev': deviations}


def configure(directory, enabled):
    settings = {
        'version': '1', 'enabled': str(enabled).lower(), 'nr_mode': 'custom',
        'target_rendered_fps': '60', 'display_hz': '60', 'display_hz_auto': 'false',
        'mfg_mode': 'off', 'mfg_quality': 'performance', 'mfg_multiplier': '2',
    }
    advanced = {
        'version': '1', 'precision_auto': 'false', 'precision': 'fp8',
        'appearance_style': 'default', 'placement': 'pre_sr', 'working_scale': '1',
        'residual_enabled': 'false', 'multipass_enabled': 'false',
    }
    for filename, fields in [('nrfusion.ini', settings), ('nrfusion_advanced.ini', advanced)]:
        (directory / filename).write_text(''.join(f'{key}={value}\n' for key, value in fields.items()),
                                         encoding='utf-8')


def measure_run(args, height, repeat, enabled):
    case = f'{height}p-{repeat + 1:02d}-nr-{"on" if enabled else "off"}'
    output = args.out / case
    output.mkdir()
    directory = args.testbed.parent
    configure(directory, enabled)
    for filename in ['nrfusion.ini', 'nrfusion_advanced.ini']:
        (output / filename).write_bytes((directory / filename).read_bytes())
    runtime_log = directory / 'nrfusion.log'
    log_offset = runtime_log.stat().st_size if runtime_log.exists() else 0
    width = {720: 1280, 1080: 1920}[height]
    command = [str(args.testbed), '--require-nrfusion-proxy', '--deterministic-motion',
               '--width', str(width), '--height', str(height), '--frames', str(args.frames),
               '--warmup', str(args.warmup), '--csv', str(output / 'evaluation.csv')]
    if enabled:
        command.append('--require-nr')
    if repeat == 0:
        command.extend(['--capture', str(output / 'calculated.ppm')])
    print(f'RUN {case}', flush=True)
    with (output / 'run.log').open('wb') as log:
        completed = subprocess.run(command, cwd=directory, stdout=log,
                                   stderr=subprocess.STDOUT, timeout=args.timeout)
    if completed.returncode:
        raise RuntimeError(f'{case} failed ({completed.returncode}); see {output / "run.log"}')
    with runtime_log.open('rb') as log:
        log.seek(log_offset)
        runtime_text = log.read().decode('utf-8', errors='replace')
    (output / 'runtime.log').write_text(runtime_text, encoding='utf-8')
    if 'Captured game NGX feature=' in runtime_text:
        raise RuntimeError(f'{case}: driver hook also captured direct-proxy feature; double NR risk')
    for filename in ['nrfusion.ini', 'nrfusion_advanced.ini']:
        if (output / filename).read_bytes() != (directory / filename).read_bytes():
            raise RuntimeError(f'{case}: configuration changed during measurement')
    log_text = (output / 'run.log').read_text(encoding='utf-8', errors='replace')
    if f'evaluations={args.frames};' not in log_text or '[PRESENT]' in log_text:
        raise RuntimeError(f'{case}: incomplete evaluations or abnormal presentation')
    with (output / 'evaluation.csv').open(newline='') as source:
        rows = list(csv.DictReader(source))
    if [int(row['frame']) for row in rows] != list(range(args.warmup, args.frames)):
        raise RuntimeError(f'{case}: missing/duplicate frames')
    dimensions = (width * 2 // 3, height * 2 // 3, width, height)
    for row in rows:
        actual = tuple(int(row[field]) for field in
                       ['input_width', 'input_height', 'output_width', 'output_height'])
        if actual != dimensions:
            raise RuntimeError(f'{case}: unexpected dimensions {actual}')
        if enabled and (int(row['nr_passes']) != 1 or int(row['nr_successes']) != 1):
            raise RuntimeError(f'{case}: NR did not execute exactly once successfully')
    metrics = {}
    columns = ['ngx_evaluation_gpu_ms', 'frame_wall_ms'] + (['nr_gpu_ms'] if enabled else [])
    for column in columns:
        values = [float(row[column]) for row in rows]
        if any(not math.isfinite(value) or value <= 0 for value in values):
            raise RuntimeError(f'{case}: invalid GPU/wall timing {column}')
        metrics[column] = summarize(values)
    result = {'case': case, 'height': height, 'repeat': repeat + 1, 'nr_enabled': enabled,
              'input': list(dimensions[:2]), 'output': list(dimensions[2:]),
              'command': command, 'metrics_ms': metrics}
    if repeat == 0:
        result['capture'] = save_capture(output / 'calculated.ppm')
    write_json(output / 'summary.json', result)
    print(f'OK {case}: NGX p50={metrics["ngx_evaluation_gpu_ms"]["p50"]:.4f} ms'
          + (f', NR p50={metrics["nr_gpu_ms"]["p50"]:.4f} ms' if enabled else ''), flush=True)
    return result


def aggregate(runs):
    results = {}
    for height in [720, 1080]:
        resolution = {}
        for enabled in [False, True]:
            selected = [run for run in runs if run['height'] == height and run['nr_enabled'] == enabled]
            metrics = {}
            for column in selected[0]['metrics_ms']:
                medians = [run['metrics_ms'][column]['p50'] for run in selected]
                median = statistics.median(medians)
                metrics[column] = {'median_run_p50_ms': median,
                    'median_run_p95_ms': statistics.median([run['metrics_ms'][column]['p95'] for run in selected]),
                    'run_p50_ms': medians, 'run_span_percent': 100 * (max(medians) - min(medians)) / median,
                    'run_cv_percent': 100 * statistics.stdev(medians) / statistics.mean(medians)}
            resolution['nr_on' if enabled else 'nr_off'] = metrics
        on = resolution['nr_on']['ngx_evaluation_gpu_ms']['median_run_p50_ms']
        off = resolution['nr_off']['ngx_evaluation_gpu_ms']['median_run_p50_ms']
        resolution['ngx_added_cost_ms'] = on - off
        results[str(height)] = resolution
    return results


def compare(baseline, current):
    for field in ['settings', 'gpu_identity', 'power_scheme', 'ac_power', 'frames', 'warmup',
                  'repeats', 'payload_hashes']:
        if field == 'payload_hashes':
            for filename, digest in baseline[field].items():
                if filename.endswith(('version.dll', 'nvngx.dll_dlssnr.dll')):
                    continue
                if current[field].get(filename) != digest:
                    raise RuntimeError(f'Comparison input/runtime changed: {filename}')
        elif baseline[field] != current[field]:
            raise RuntimeError(f'Comparison environment/protocol changed: {field}')
    gains = {}
    for height in ['720', '1080']:
        old = baseline['results'][height]['nr_on']['nr_gpu_ms']
        new = current['results'][height]['nr_on']['nr_gpu_ms']
        reduction = 100 * (1 - new['median_run_p50_ms'] / old['median_run_p50_ms'])
        spread = max(old['run_span_percent'], new['run_span_percent'])
        gains[height] = {'nr_time_reduction_percent': reduction,
                        'observed_run_span_percent': spread,
                        'exceeds_observed_spread': reduction > spread,
                        'qualification': 'Requires paired A/B rerun and output quality check before accepting gain'}
    return gains


def benchmark(args):
    args.out = args.out.resolve()
    args.testbed = args.testbed.resolve()
    if args.frames <= args.warmup or args.warmup < 60 or args.repeats < 2:
        raise RuntimeError('Need frames > warmup >= 60 and repeats >= 2')
    args.out.mkdir(parents=True, exist_ok=False)
    directory = args.testbed.parent
    payloads = [args.testbed, directory / 'version.dll', directory / 'nvngx_dlss.dll',
                directory / 'NRFusion/internal/nvngx_dlssnr.dll',
                directory / 'NRFusion/internal/nvngx.dll_dlssnr.dll',
                directory / 'assets/nvidia-dlss5-requiem-off.jpeg',
                directory / 'assets/nvidia-dlss5-requiem-on.jpeg']
    payload_hashes = {str(path.relative_to(directory)): sha256(path) for path in payloads}
    gpu = subprocess.check_output(['nvidia-smi', '--query-gpu=name,uuid,driver_version',
                                  '--format=csv,noheader'], text=True).strip()
    power_scheme = subprocess.check_output(['powercfg', '/getactivescheme'], text=True).strip()
    power_status = (ctypes.c_ubyte * 12)()
    if not ctypes.windll.kernel32.GetSystemPowerStatus(power_status):
        raise RuntimeError('Cannot determine AC power state')
    if power_status[0] != 1:
        raise RuntimeError('Connect AC power before measuring this baseline')
    original = {name: (directory / name).read_bytes() if (directory / name).exists() else None
                for name in ['nrfusion.ini', 'nrfusion_advanced.ini']}
    write_json(args.out / 'original-config.json',
               {name: value.decode('utf-8-sig') if value is not None else None for name, value in original.items()})
    metadata = {'schema': 1, 'created_utc': datetime.now(timezone.utc).isoformat(),
                'settings': SETTINGS, 'gpu_identity': gpu, 'os': platform.platform(),
                'power_scheme': power_scheme, 'frames': args.frames, 'warmup': args.warmup,
                'ac_power': True, 'benchmark_script_sha256': sha256(Path(__file__)),
                'repeats': args.repeats, 'payload_hashes': payload_hashes,
                'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'source_hashes': {str(path.relative_to(ROOT)): sha256(path) for path in
                    list((ROOT / 'src').glob('*.cpp')) + list((ROOT / 'src').glob('*.hpp'))
                    + list((ROOT / 'tools/requiem_game').glob('*.*'))}}
    write_json(args.out / 'protocol.json', metadata)
    telemetry_file = (args.out / 'gpu-telemetry.csv').open('wb')
    telemetry = subprocess.Popen(['nvidia-smi', '--query-gpu=timestamp,name,pstate,temperature.gpu,'
        'clocks.current.graphics,clocks.current.memory,power.draw,utilization.gpu',
        '--format=csv', '-l', '1'], stdout=telemetry_file, stderr=subprocess.STDOUT)
    runs = []
    try:
        for repeat in range(args.repeats):
            for height in ([720, 1080] if repeat % 2 == 0 else [1080, 720]):
                for enabled in ([False, True] if repeat % 2 == 0 else [True, False]):
                    runs.append(measure_run(args, height, repeat, enabled))
    finally:
        telemetry.terminate()
        telemetry.wait(timeout=10)
        telemetry_file.close()
        for name, contents in original.items():
            if contents is None:
                (directory / name).unlink(missing_ok=True)
            else:
                (directory / name).write_bytes(contents)
    for relative, digest in payload_hashes.items():
        if sha256(directory / relative) != digest:
            raise RuntimeError(f'Payload changed during measurement: {relative}')
    metadata.update({'runs': runs, 'results': aggregate(runs),
                     'qualification': 'Controlled Requiem pipeline baseline; not actual gameplay qualification'})
    from PIL import Image, ImageChops, ImageStat
    for height in [720, 1080]:
        prefix = args.out / f'{height}p-01-nr-'
        with Image.open(str(prefix) + 'on/calculated.png') as on, Image.open(str(prefix) + 'off/calculated.png') as off:
            difference = ImageStat.Stat(ImageChops.difference(on, off)).mean
            if max(difference) <= 0:
                raise RuntimeError(f'{height}p: NR on/off output is identical')
            metadata['results'][str(height)]['on_off_mean_absolute_rgb_difference'] = difference
    if args.compare:
        metadata['comparison'] = compare(json.loads(args.compare.read_text(encoding='utf-8')), metadata)
    write_json(args.out / 'baseline.json', metadata)
    if args.save_baseline:
        args.save_baseline.mkdir(parents=True, exist_ok=False)
        write_json(args.save_baseline / 'baseline.json', metadata)
        with zipfile.ZipFile(args.save_baseline / 'evidence.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in args.out.rglob('*'):
                if path.is_file() and path.suffix != '.ppm':
                    archive.write(path, path.relative_to(args.out))
    print(json.dumps(metadata['results'], indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--testbed', type=Path, default=ROOT / 'dist/RequiemGame/RequiemGame.exe')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=1200)
    parser.add_argument('--warmup', type=int, default=300)
    parser.add_argument('--repeats', type=int, default=5)
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--save-baseline', type=Path)
    parser.add_argument('--compare', type=Path)
    try:
        benchmark(parser.parse_args())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'Benchmark failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
