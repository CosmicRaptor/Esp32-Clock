import base64
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import auth  # noqa: E402
from auth import BadJwt  # noqa: E402

TEAM = "https://bhoomi.cloudflareaccess.com"
AUD = "a" * 64
NOW = 1_800_000_000


def part(obj) -> str:
    return base64.urlsafe_b64encode(json.dumps(obj).encode()).rstrip(b"=").decode()


def jwt(header=None, claims=None, sig=b"\x01\x02\x03") -> str:
    h = {"alg": "RS256", "kid": "k1", "typ": "JWT"} if header is None else header
    c = {"aud": [AUD], "iss": TEAM, "exp": NOW + 600, "nbf": NOW - 5, "email": "a@b.c"} if claims is None else claims
    return f"{part(h)}.{part(c)}.{base64.urlsafe_b64encode(sig).rstrip(b'=').decode()}"


class HttpsOnly(unittest.TestCase):
    def test_local_hosts(self):
        for h in ("localhost", "127.0.0.1", "[::1]", "192.168.2.13", "10.0.0.5", "172.20.1.1", "x.localhost"):
            self.assertTrue(auth.is_local(h), h)

    def test_public_hosts(self):
        for h in ("bhoomi-clock.bhoomi-clock-portal.workers.dev", "8.8.8.8", "172.32.0.1", "192.169.0.1",
                  "127.0.0.1.nip.io", "", None):
            self.assertFalse(auth.is_local(h), h)

    def test_https_url(self):
        self.assertEqual(auth.https_url("http://x.workers.dev/a?b=1"), "https://x.workers.dev/a?b=1")


class AccessJwt(unittest.TestCase):
    def test_split(self):
        header, claims, signed, sig = auth.split_jwt(jwt())
        self.assertEqual(header["kid"], "k1")
        self.assertEqual(claims["email"], "a@b.c")
        self.assertEqual(sig, b"\x01\x02\x03")
        self.assertEqual(signed, jwt().rsplit(".", 1)[0].encode())

    def test_malformed(self):
        for t in (None, "", "a.b", "a.b.c.d", "!!.??.**", part([1]) + "." + part({}) + ".AA"):
            with self.assertRaises(BadJwt, msg=t):
                auth.split_jwt(t)

    def test_rejects_other_algorithms(self):
        for h in ({"alg": "none", "kid": "k1"}, {"alg": "HS256", "kid": "k1"}, {"alg": "RS256"}):
            with self.assertRaises(BadJwt):
                auth.split_jwt(jwt(header=h))

    def test_good_claims(self):
        _, claims, _, _ = auth.split_jwt(jwt())
        auth.check_access_claims(claims, AUD, TEAM + "/", NOW)
        auth.check_access_claims(dict(claims, aud=AUD), AUD, TEAM, NOW)  # aud may be a bare string

    def test_bad_claims(self):
        good = {"aud": [AUD], "iss": TEAM, "exp": NOW + 600}
        for bad in ({"aud": ["other"]}, {"aud": None}, {"iss": "https://evil.cloudflareaccess.com"},
                    {"exp": NOW - 120}, {"exp": None}, {"exp": "soon"}, {"nbf": NOW + 120}):
            with self.assertRaises(BadJwt, msg=bad):
                auth.check_access_claims(dict(good, **bad), AUD, TEAM, NOW)

    def test_leeway(self):
        auth.check_access_claims({"aud": [AUD], "iss": TEAM, "exp": NOW - 30, "nbf": NOW + 30}, AUD, TEAM, NOW)


if __name__ == "__main__":
    unittest.main()
