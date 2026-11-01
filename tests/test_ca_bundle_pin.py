# SPDX-License-Identifier: GPL-3.0-or-later
"""The root-certificate bundle tools/stage-wine.py stages is the one NOTICE.md names, and its pin is a SHA-256.

shortcut: checks the staged copy only when .deps/ca-bundle already holds it (no network here); stage-wine.py itself
refuses a download that differs from its pin."""
import hashlib
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CaBundlePin(unittest.TestCase):
    def setUp(self):
        stage = (ROOT / 'tools/stage-wine.py').read_text()
        self.url = re.search(r"CA_BUNDLE_URL = '([^']+)'", stage).group(1)
        self.digest = re.search(r"CA_BUNDLE_SHA256 = '([0-9a-f]+)'", stage).group(1)

    def test_pin_is_a_sha256_of_a_dated_curl_extract(self):
        self.assertEqual(len(self.digest), 64)
        self.assertRegex(self.url, r'^https://curl\.se/ca/cacert-\d{4}-\d{2}-\d{2}\.pem$')

    def test_notice_names_the_same_bundle(self):
        notice = (ROOT / 'NOTICE.md').read_text()
        self.assertIn(self.url, notice)
        self.assertIn(self.digest, notice)

    def test_a_bundle_already_fetched_matches_the_pin(self):
        bundle = ROOT / '.deps/ca-bundle' / self.url.rsplit('/', 1)[1]
        if not bundle.is_file():
            self.skipTest('not fetched on this host')
        self.assertEqual(hashlib.sha256(bundle.read_bytes()).hexdigest(), self.digest)


if __name__ == '__main__':
    unittest.main()
