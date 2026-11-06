"""Admin session cookies and token checks. Pure Python, unit-testable without Workers.

The session cookie is "v1.<expiry>.<hmac>", signed with a key derived from ADMIN_PASSWORD,
so changing the password logs every browser out. Nothing secret is stored in the cookie.
"""

import hashlib
import hmac
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
