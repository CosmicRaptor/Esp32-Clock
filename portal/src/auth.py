"""Admin session cookies and token checks. Pure Python, unit-testable without Workers.

The session cookie is "v1.<expiry>.<hmac>", signed with a key derived from ADMIN_PASSWORD,
so changing the password logs every browser out. Nothing secret is stored in the cookie.
"""

import base64
import binascii
import hashlib
import hmac
import json
import time

COOKIE = "clock_session"
SESSION_DAYS = 30


def _key(password: str) -> bytes:
    return hmac.new(password.encode(), b"bhoomi-clock session v1", hashlib.sha256).digest()


def _sign(password: str, payload: str) -> str:
    return hmac.new(_key(password), payload.encode(), hashlib.sha256).hexdigest()


def make_session(password: str, now: float | None = None) -> str:
    expiry = int(now if now is not None else time.time()) + SESSION_DAYS * 86400
    payload = f"v1.{expiry}"
    return f"{payload}.{_sign(password, payload)}"


def check_session(password: str, token: str | None, now: float | None = None) -> bool:
    if not password or not token:
        return False
    parts = token.split(".")
    if len(parts) != 3 or parts[0] != "v1" or not parts[1].isdigit():
        return False
    payload = f"{parts[0]}.{parts[1]}"
    if not hmac.compare_digest(_sign(password, payload), parts[2]):
        return False
    return int(parts[1]) > (now if now is not None else time.time())


# ---------- OAuth `state` for "Connect Spotify" ----------
# The admin cookie is SameSite=Strict, so it is NOT sent when Spotify redirects the browser back to
# us. Instead the login step (which does have the cookie) hands out a signed, short-lived `state`,
# and the callback trusts only a valid one. Different key context from the session, so the two
# can't stand in for each other.

OAUTH_STATE_MINUTES = 10


def _oauth_sign(password: str, payload: str) -> str:
    key = hmac.new(password.encode(), b"bhoomi-clock oauth-state v1", hashlib.sha256).digest()
    return hmac.new(key, payload.encode(), hashlib.sha256).hexdigest()


def make_oauth_state(password: str, side: str, nonce: str, now: float | None = None) -> str:
    expiry = int((now if now is not None else time.time()) + OAUTH_STATE_MINUTES * 60)
    payload = f"o1.{side}.{expiry}.{nonce}"
    return f"{payload}.{_oauth_sign(password, payload)}"


def check_oauth_state(password: str, state: str | None, now: float | None = None) -> str | None:
    """The side the state was issued for, or None if it is forged, expired or malformed."""
    if not password or not state:
        return None
    parts = state.split(".")
    if len(parts) != 5 or parts[0] != "o1" or not parts[2].isdigit():
        return None
    payload = ".".join(parts[:4])
    if not hmac.compare_digest(_oauth_sign(password, payload), parts[4]):
        return None
    if int(parts[2]) <= (now if now is not None else time.time()):
        return None
    return parts[1]


def same_secret(expected: str | None, given: str | None) -> bool:
    """Constant-time compare; an unset secret never matches."""
    if not expected or given is None:
        return False
    return hmac.compare_digest(expected.encode(), given.encode())


def bearer(header: str | None) -> str | None:
    if header and header[:7].lower() == "bearer ":
        return header[7:].strip()
    return None


def read_cookie(header: str | None, name: str = COOKIE) -> str | None:
    for part in (header or "").split(";"):
        k, _, v = part.strip().partition("=")
        if k == name:
            return v
    return None


def session_cookie(token: str, secure: bool) -> str:
    c = f"{COOKIE}={token}; Path=/; HttpOnly; SameSite=Strict; Max-Age={SESSION_DAYS * 86400}"
    return c + "; Secure" if secure else c


def clear_cookie(secure: bool) -> str:
    c = f"{COOKIE}=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0"
    return c + "; Secure" if secure else c


# ---------- HTTPS only ----------

def is_local(host: str | None) -> bool:
    """`wrangler dev` on this Mac or the LAN (plain http, no Access in front). Never true on workers.dev."""
    h = (host or "").strip("[]").lower()
    if h in ("localhost", "127.0.0.1", "::1") or h.endswith(".localhost"):
        return True
    parts = h.split(".")
    if len(parts) != 4 or not all(p.isdigit() and int(p) < 256 for p in parts):
        return False
    a, b = int(parts[0]), int(parts[1])
    return a == 10 or a == 127 or (a == 192 and b == 168) or (a == 172 and 16 <= b <= 31)


def https_url(url: str) -> str:
    return "https://" + url.split("://", 1)[1]


# ---------- Cloudflare Access ----------
# Access sits in front of the whole site and signs every request it lets through (a person who
# logged in, or the clock's service token) with a JWT in the Cf-Access-Jwt-Assertion header. The
# Worker re-checks it, so the site stays closed even if Access is ever switched off or bypassed.
# The RS256 signature itself is checked with WebCrypto in entry.py; the rest is here.

ACCESS_HEADER = "cf-access-jwt-assertion"
ACCESS_LEEWAY = 60  # s of clock skew allowed on exp / nbf


class BadJwt(Exception):
    pass


def b64url(data: str) -> bytes:
    return base64.urlsafe_b64decode(data + "=" * (-len(data) % 4))


def split_jwt(token: str | None) -> tuple[dict, dict, bytes, bytes]:
    """(header, claims, signed bytes, signature). Raises BadJwt if it isn't a well-formed RS256 JWT."""
    if not token:
        raise BadJwt("no Access token")
    parts = token.split(".")
    if len(parts) != 3:
        raise BadJwt("malformed token")
    try:
        header = json.loads(b64url(parts[0]))
        claims = json.loads(b64url(parts[1]))
        signature = b64url(parts[2])
    except (ValueError, binascii.Error):
        raise BadJwt("malformed token") from None
    if not isinstance(header, dict) or not isinstance(claims, dict):
        raise BadJwt("malformed token")
    if header.get("alg") != "RS256" or not isinstance(header.get("kid"), str):
        raise BadJwt("unexpected algorithm")
    return header, claims, f"{parts[0]}.{parts[1]}".encode(), signature


def check_access_claims(claims: dict, aud: str, issuer: str, now: float | None = None) -> None:
    """Raises BadJwt unless the token is for this Access application, from our team, and current."""
    now = now if now is not None else time.time()
    got_aud = claims.get("aud")
    auds = got_aud if isinstance(got_aud, list) else [got_aud]
    if aud not in auds:
        raise BadJwt("token is for another application")
    if claims.get("iss") != issuer.rstrip("/"):
        raise BadJwt("token is from another team")
    exp = claims.get("exp")
    if not isinstance(exp, (int, float)) or exp + ACCESS_LEEWAY < now:
        raise BadJwt("token expired")
    nbf = claims.get("nbf")
    if isinstance(nbf, (int, float)) and nbf - ACCESS_LEEWAY > now:
        raise BadJwt("token not valid yet")

