import os
import sys
import unittest
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import auth  # noqa: E402
import clock_pb2 as pb  # noqa: E402
import spotify  # noqa: E402
import wire  # noqa: E402

H64 = "ab67616d00004851" + "a" * 24      # 40 hex chars, like Spotify's image ids
H300 = "ab67616d00001e02" + "a" * 24
H640 = "ab67616d0000b273" + "a" * 24


def images():
    return [{"url": f"https://i.scdn.co/image/{h}", "width": w, "height": w}
            for h, w in ((H640, 640), (H300, 300), (H64, 64))]


def track(**over):
    item = {"type": "track", "name": "Blinding Lights", "artists": [{"name": "The Weeknd"}],
            "album": {"images": images()}}
    item.update(over)
    return {"is_playing": True, "currently_playing_type": "track", "item": item}


class ParsePlayingTests(unittest.TestCase):
    def test_track(self):
        self.assertEqual(spotify.parse_playing(200, track()),
                         {"status": "playing", "title": "Blinding Lights", "artist": "The Weeknd", "art": H64})

    def test_paused(self):
        d = track()
        d["is_playing"] = False
        self.assertEqual(spotify.parse_playing(200, d)["status"], "paused")

    def test_nothing_playing(self):
        self.assertEqual(spotify.parse_playing(204, None)["status"], "off")

    def test_ad_or_private_session_has_no_item(self):
        self.assertEqual(spotify.parse_playing(200, {"is_playing": True, "item": None})["status"], "off")

    def test_several_artists(self):
        d = track(artists=[{"name": "A"}, {"name": "B"}, {"name": "C"}])
        self.assertEqual(spotify.parse_playing(200, d)["artist"], "A, B, C")

    def test_episode_uses_publisher_and_its_own_images(self):
        d = {"is_playing": True, "item": {"type": "episode", "name": "Ep 12", "images": images(),
                                          "show": {"name": "The Show", "publisher": "Some Network"}}}
        out = spotify.parse_playing(200, d)
        self.assertEqual((out["title"], out["artist"], out["art"]), ("Ep 12", "Some Network", H64))

    def test_garbage_is_off_not_a_crash(self):
        for status, data in ((200, []), (200, {"item": 5}), (500, {}), (200, {"item": {"name": None}})):
            out = spotify.parse_playing(status, data)
            self.assertIn(out["status"], ("off", "playing", "paused"))

    def test_no_usable_cover(self):
        self.assertEqual(spotify.parse_playing(200, track(album={"images": []}))["art"], "")
        self.assertEqual(spotify.parse_playing(200, track(album={}))["art"], "")


class PickArtTests(unittest.TestCase):
    def test_smallest_that_is_big_enough(self):
        self.assertEqual(spotify.pick_art(images()), H64)
        self.assertEqual(spotify.pick_art(images()[:2]), H300)

    def test_only_spotify_cdn_ids_are_accepted(self):
        evil = [{"url": "https://evil.example/image/" + H64, "width": 64},
                {"url": "https://i.scdn.co/image/../../etc/passwd", "width": 64},
                {"url": "http://i.scdn.co/image/" + H64, "width": 64},
                {"url": "https://i.scdn.co/image/" + H64 + "?x=1", "width": 64},
                {"url": "https://i.scdn.co/image/" + H64.upper(), "width": 64}]
        self.assertEqual(spotify.pick_art(evil), "")

    def test_missing_sizes(self):
        self.assertEqual(spotify.pick_art([{"url": f"https://i.scdn.co/image/{H64}"}]), "")
        self.assertEqual(spotify.pick_art(None), "")

    def test_valid_art_id(self):
        self.assertTrue(spotify.valid_art_id(H64))
        for bad in ("", "abc", H64 + "0", H64[:-1], "g" * 40, None, "../" + H64[3:]):
            self.assertFalse(spotify.valid_art_id(bad))


class AsciiTextTests(unittest.TestCase):
    def test_accents_and_punctuation(self):
        self.assertEqual(spotify.ascii_text("Beyoncé – Déjà Vu’s “Best”…"), 'Beyonce - Deja Vu\'s "Best"...')

    def test_other_scripts_collapse_to_one_question_mark(self):
        self.assertEqual(spotify.ascii_text("夜に駆ける"), "?")
        self.assertEqual(spotify.ascii_text("Yoasobi 夜に駆ける 😀"), "Yoasobi ? ?")

    def test_shortened_with_dots(self):
        out = spotify.ascii_text("x" * 100)
        self.assertEqual(len(out), spotify.TEXT_MAX)
        self.assertTrue(out.endswith("..."))

    def test_whitespace_and_control_characters(self):
        self.assertEqual(spotify.ascii_text("  a \n\t b\x00c  "), "a b?c")

    def test_not_text(self):
        self.assertEqual(spotify.ascii_text(None), "")
        self.assertEqual(spotify.ascii_text(5), "")

    def test_empty_title_becomes_a_question_mark(self):
        self.assertEqual(spotify.parse_playing(200, track(name="​"))["title"], "?")


class TokenTests(unittest.TestCase):
    def test_first_grant(self):
        rec = spotify.token_record({"access_token": "A", "refresh_token": "R", "expires_in": 3600}, 1000)
        self.assertEqual(rec, {"refresh": "R", "access": "A", "expires": 4600})

    def test_refresh_keeps_the_old_refresh_token_unless_rotated(self):
        old = {"refresh": "R1", "access": "A0", "expires": 5}
        self.assertEqual(spotify.token_record({"access_token": "A1", "expires_in": 100}, 0, old)["refresh"], "R1")
        self.assertEqual(spotify.token_record({"access_token": "A1", "refresh_token": "R2"}, 0, old)["refresh"], "R2")

    def test_unusable_answers(self):
        self.assertIsNone(spotify.token_record({"error": "invalid_grant"}, 0))
        self.assertIsNone(spotify.token_record({"access_token": "A"}, 0))  # no refresh token anywhere
        self.assertIsNone(spotify.token_record(None, 0))

    def test_fresh_until_just_before_expiry(self):
        rec = {"refresh": "R", "access": "A", "expires": 1000}
        self.assertTrue(spotify.token_fresh(rec, 1000 - spotify.EXPIRY_MARGIN - 1))
        self.assertFalse(spotify.token_fresh(rec, 1000 - spotify.EXPIRY_MARGIN))
        self.assertFalse(spotify.token_fresh(None, 0))

    def test_authorize_url(self):
        u = urlparse(spotify.authorize_url("cid", "https://x.dev/api/spotify/callback", "st"))
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        self.assertEqual((u.netloc, u.path), ("accounts.spotify.com", "/authorize"))
        self.assertEqual((q["client_id"], q["state"], q["scope"], q["response_type"]),
                         ("cid", "st", "user-read-currently-playing", "code"))
        self.assertEqual(q["redirect_uri"], "https://x.dev/api/spotify/callback")

    def test_basic_auth(self):
        self.assertEqual(spotify.basic_auth("id", "secret"), "Basic aWQ6c2VjcmV0")


class OAuthStateTests(unittest.TestCase):
    def test_round_trip(self):
        s = auth.make_oauth_state("pw", "her", "abc123", now=1000)
        self.assertEqual(auth.check_oauth_state("pw", s, now=1001), "her")

    def test_expires(self):
        s = auth.make_oauth_state("pw", "me", "n", now=1000)
        self.assertIsNone(auth.check_oauth_state("pw", s, now=1000 + auth.OAUTH_STATE_MINUTES * 60 + 1))

    def test_forgery_and_wrong_password(self):
        s = auth.make_oauth_state("pw", "me", "n", now=1000)
        self.assertIsNone(auth.check_oauth_state("other", s, now=1001))
        self.assertIsNone(auth.check_oauth_state("pw", s.replace(".me.", ".her."), now=1001))  # side swapped
        for junk in (None, "", "o1.me", "x.me.9999999999.n.sig", s + "0", s[:-1]):
            self.assertIsNone(auth.check_oauth_state("pw", junk, now=1001))

    def test_session_cookie_is_not_a_state(self):
        self.assertIsNone(auth.check_oauth_state("pw", auth.make_session("pw", now=1000), now=1001))
        self.assertFalse(auth.check_session("pw", auth.make_oauth_state("pw", "me", "n", now=1000), now=1001))


class NowPlayingWireTests(unittest.TestCase):
    def test_round_trip(self):
        left = {"status": "playing", "title": "Blinding Lights", "artist": "The Weeknd", "art": H64}
        right = dict(spotify.OFF)
        msg = pb.NowPlaying.FromString(wire.now_playing(left, right).SerializeToString())
        self.assertEqual((msg.left.status, msg.left.title, msg.left.artist, msg.left.art_id),
                         (pb.Listener.PLAYING, "Blinding Lights", "The Weeknd", H64))
        self.assertTrue(msg.HasField("right"))
        self.assertEqual((msg.right.status, msg.right.title, msg.right.art_id), (pb.Listener.OFF, "", ""))

    def test_paused(self):
        paused = {"status": "paused", "title": "T", "artist": "A", "art": ""}
        msg = wire.now_playing(paused, dict(spotify.OFF))
        self.assertEqual(msg.left.status, pb.Listener.PAUSED)

    def test_same_listening_same_bytes(self):  # the ETag is a hash of these bytes
        a = {"status": "playing", "title": "T", "artist": "A", "art": H64}
        self.assertEqual(wire.now_playing(a, a).SerializeToString(), wire.now_playing(dict(a), dict(a)).SerializeToString())

    def test_worst_case_fits_nanopb_buffers(self):
        worst = {"status": "playing", "title": "x" * 100, "artist": "y" * 100, "art": H64}
        clean = spotify.parse_playing(200, track(name=worst["title"], artists=[{"name": worst["artist"]}]))
        msg = wire.now_playing(clean, clean)
        for side in (msg.left, msg.right):
            self.assertLessEqual(len(side.title.encode()), 60)    # Listener.title max_size:61
            self.assertLessEqual(len(side.artist.encode()), 60)
            self.assertLessEqual(len(side.art_id), 40)
        self.assertLessEqual(len(msg.SerializeToString()), 2 * (2 + 2 + 63 + 63 + 42) + 4)


if __name__ == "__main__":
    unittest.main()
