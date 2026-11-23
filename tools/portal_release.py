#!/usr/bin/env python3
"""Track portal deployments independently of signed firmware releases."""
import argparse
import base64
import json
import os
import subprocess

from ota_signing import ROOT
from scheduled_ota import git, output

PREFIX = 'clock-portal/'
INPUTS = ['portal/src', 'portal/public', 'portal/wrangler.toml',
          'portal/pyproject.toml', 'portal/pylock.toml', 'portal/uv.lock',
          'portal/package.json', 'portal/package-lock.json', 'tools/portal_release.py']


def latest_tag():
    tags = git('for-each-ref', '--sort=-creatordate', '--format=%(refname:short)',
               'refs/tags/' + PREFIX).splitlines()
    return tags[0] if tags else None


def prepare():
    tag = latest_tag()
    changed = tag is None or bool(git('diff', '--name-only', tag, 'HEAD', '--', *INPUTS))
    output(changed='true' if changed else 'false', source=git('rev-parse', 'HEAD'))


def deploy():
    required = ('CLOUDFLARE_ACCOUNT_ID', 'CLOUDFLARE_API_TOKEN', 'GITHUB_TOKEN', 'GITHUB_REPOSITORY')
    if not all(os.environ.get(key) for key in required):
        raise SystemExit('Portal CI deployment requires CLOUDFLARE_ACCOUNT_ID and CLOUDFLARE_API_TOKEN repository secrets and a GitHub token.')
    # Keep deployment credentials out of the tagging process and vice versa.
    deploy_env = os.environ.copy()
    deploy_env.pop('GITHUB_TOKEN', None)
    subprocess.run(['npm', 'run', 'deploy'], cwd=ROOT / 'portal', env=deploy_env, check=True)
    source = git('rev-parse', 'HEAD')
    tag = PREFIX + source
    subprocess.run(['git', '-c', 'user.name=Clock portal',
                    '-c', 'user.email=clock-portal@users.noreply.github.com',
                    'tag', '-a', tag, '-m', json.dumps({'source': source, 'worker': 'bhoomi-clock'})],
                   cwd=ROOT, check=True)
    env = os.environ.copy()
    token = base64.b64encode(('x-access-token:' + env.pop('GITHUB_TOKEN')).encode()).decode()
    env.pop('CLOUDFLARE_API_TOKEN', None)
    env.update(GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='http.https://github.com/.extraheader',
               GIT_CONFIG_VALUE_0='AUTHORIZATION: basic ' + token)
    subprocess.run(['git', 'push', 'https://github.com/' + env['GITHUB_REPOSITORY'] + '.git',
                    'refs/tags/' + tag], cwd=ROOT, env=env, check=True)
    summary = f'Portal deployed from {source}; recorded {tag}.\n'
    print(summary)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as stream:
            stream.write(summary)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'deploy'])
    args = parser.parse_args()
    prepare() if args.action == 'prepare' else deploy()
