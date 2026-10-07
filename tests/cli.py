import json
import subprocess


def run(*args):
    return subprocess.run(['./dockctl', *args], text=True, capture_output=True)


features = run('features', '--json')
assert features.returncode == 0
rows = json.loads(features.stdout)
assert next(x for x in rows if x['id'] == 'silence_watch')['status'] == 'experimental'
for args in [('watch', '--seconds', '0'), ('watch', '--seconds', '3601'),
             ('watch', '--seconds', 'garbage'), ('info', '--silence'),
             ('auto', '--seconds', '60'), ('watch', '--unknown'), ('reset',),
             ('watch', '--stable-seconds', '30'),
             ('watch', '--silence', '--stable-seconds', '0'),
             ('watch', '--silence', '--stable-seconds', '121'),
             ('watch', '--silence', '--silence-seconds', '301'),
             ('watch', '--silence', '--silence-seconds', '1'),
             ('watch', '--silence', '--silence-seconds', '-1'),
             ('watch', '--silence', '--cooling-seconds', '0'),
             ('watch', '--silence', '--cooling-seconds', '29'),
             ('watch', '--silence', '--ventilate-at', '42,47'),
             ('watch', '--silence', '--ventilate-at', '42,47,68,70'),
             ('watch', '--silence', '--ventilate-at', '42,47,68x'),
             ('watch', '--silence', '--ventilate-at', '42,47,73'),
             ('watch', '--silence', '--resume-at', '45,50,70'),
             ('info', '--ventilate-at', '42,47,68')]:
    result = run(*args)
    assert result.returncode == 2, (args, result)
assert run('--help').returncode == 0
print('OK: CLI arguments and JSON feature inventory (no hardware).')
