import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import portal_release as portal


class PortalReleaseTests(unittest.TestCase):
    def test_first_run_deploys_and_always_exports_source(self):
        with patch.object(portal, 'latest_tag', return_value=None), \
             patch.object(portal, 'git', return_value='a'*40), patch.object(portal, 'output') as output:
            portal.prepare()
            output.assert_called_once_with(changed='true', source='a'*40)

    def test_real_git_history_distinguishes_runtime_changes_from_docs_and_firmware(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def git(*args):
                return subprocess.check_output(['git', *args], cwd=root, text=True, stderr=subprocess.DEVNULL).strip()
            git('init'); git('config', 'user.name', 'Test'); git('config', 'user.email', 'test@example.com')
            (root/'portal/src').mkdir(parents=True)
            (root/'portal/src/entry.py').write_text('original')
            git('add', '.'); git('commit', '-m', 'initial')
            git('tag', '-a', 'clock-portal/initial', '-m', 'deployed')
            def prepare():
                with patch.object(portal, 'git', side_effect=git), patch.object(portal, 'output') as output:
                    portal.prepare()
                    return output.call_args.kwargs['changed']
            self.assertEqual(prepare(), 'false')
            (root/'portal/README.md').write_text('documentation')
            (root/'firmware').mkdir(); (root/'firmware/app.cpp').write_text('firmware only')
            git('add', '.'); git('commit', '-m', 'docs and firmware')
            self.assertEqual(prepare(), 'false')
            (root/'portal/src/entry.py').write_text('updated')
            git('add', '.'); git('commit', '-m', 'portal update')
            self.assertEqual(prepare(), 'true')
            # A failed deploy leaves the earlier marker, so the next run retries.
            self.assertEqual(prepare(), 'true')
            git('tag', '-d', 'clock-portal/initial')
            git('tag', '-a', 'clock-portal/updated', '-m', 'deployed')
            self.assertEqual(prepare(), 'false')

    def test_failed_deploy_does_not_create_or_push_a_marker(self):
        env = dict(CLOUDFLARE_ACCOUNT_ID='test', CLOUDFLARE_API_TOKEN='private-test',
                   GITHUB_TOKEN='private-github', GITHUB_REPOSITORY='owner/repo')
        with patch.dict(os.environ, env), patch.object(portal.subprocess, 'run',
                  side_effect=subprocess.CalledProcessError(1, ['npm'])) as run:
            with self.assertRaises(subprocess.CalledProcessError): portal.deploy()
            self.assertEqual(run.call_count, 1)
            self.assertNotIn('GITHUB_TOKEN', run.call_args.kwargs['env'])

    def test_missing_cloudflare_credentials_fail_before_deploy(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(portal.subprocess, 'run') as run:
            with self.assertRaisesRegex(SystemExit, 'CLOUDFLARE_API_TOKEN'): portal.deploy()
            run.assert_not_called()

    def test_success_records_source_after_deployment(self):
        env = dict(CLOUDFLARE_ACCOUNT_ID='test', CLOUDFLARE_API_TOKEN='private-test',
                   GITHUB_TOKEN='private-github', GITHUB_REPOSITORY='owner/repo')
        with patch.dict(os.environ, env, clear=True), patch.object(portal, 'git', return_value='a'*40), \
             patch.object(portal.subprocess, 'run') as run, patch('builtins.print'):
            portal.deploy()
            commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual(commands[0], ['npm', 'run', 'deploy'])
            self.assertIn('tag', commands[1])
            self.assertIn('refs/tags/clock-portal/' + 'a'*40, commands[2])
            self.assertNotIn('CLOUDFLARE_API_TOKEN', run.call_args_list[2].kwargs['env'])


if __name__ == '__main__': unittest.main()
