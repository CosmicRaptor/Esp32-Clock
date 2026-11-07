from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DeviceState(_message.Message):
    __slots__ = ("version", "note", "photo", "alarms", "nowplaying", "faces", "pinned_face", "default_face")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    NOTE_FIELD_NUMBER: _ClassVar[int]
    PHOTO_FIELD_NUMBER: _ClassVar[int]
    ALARMS_FIELD_NUMBER: _ClassVar[int]
    NOWPLAYING_FIELD_NUMBER: _ClassVar[int]
    FACES_FIELD_NUMBER: _ClassVar[int]
    PINNED_FACE_FIELD_NUMBER: _ClassVar[int]
    DEFAULT_FACE_FIELD_NUMBER: _ClassVar[int]
    version: int
    note: Note
    photo: Photo
    alarms: _containers.RepeatedCompositeFieldContainer[Alarm]
    nowplaying: NowPlaying
    faces: _containers.RepeatedCompositeFieldContainer[FaceRef]
    pinned_face: str
    default_face: str
    def __init__(self, version: _Optional[int] = ..., note: _Optional[_Union[Note, _Mapping]] = ..., photo: _Optional[_Union[Photo, _Mapping]] = ..., alarms: _Optional[_Iterable[_Union[Alarm, _Mapping]]] = ..., nowplaying: _Optional[_Union[NowPlaying, _Mapping]] = ..., faces: _Optional[_Iterable[_Union[FaceRef, _Mapping]]] = ..., pinned_face: _Optional[str] = ..., default_face: _Optional[str] = ...) -> None: ...

class Note(_message.Message):
    __slots__ = ("text", "sent_at")
    TEXT_FIELD_NUMBER: _ClassVar[int]
    SENT_AT_FIELD_NUMBER: _ClassVar[int]
    text: str
    sent_at: int
    def __init__(self, text: _Optional[str] = ..., sent_at: _Optional[int] = ...) -> None: ...

class Photo(_message.Message):
    __slots__ = ("id", "sent_at", "size")
    ID_FIELD_NUMBER: _ClassVar[int]
    SENT_AT_FIELD_NUMBER: _ClassVar[int]
    SIZE_FIELD_NUMBER: _ClassVar[int]
    id: str
    sent_at: int
    size: int
    def __init__(self, id: _Optional[str] = ..., sent_at: _Optional[int] = ..., size: _Optional[int] = ...) -> None: ...

class Alarm(_message.Message):
    __slots__ = ("id", "hour", "minute", "days", "date", "label", "sound")
    ID_FIELD_NUMBER: _ClassVar[int]
    HOUR_FIELD_NUMBER: _ClassVar[int]
    MINUTE_FIELD_NUMBER: _ClassVar[int]
    DAYS_FIELD_NUMBER: _ClassVar[int]
    DATE_FIELD_NUMBER: _ClassVar[int]
    LABEL_FIELD_NUMBER: _ClassVar[int]
    SOUND_FIELD_NUMBER: _ClassVar[int]
    id: str
    hour: int
    minute: int
    days: int
    date: Date
    label: str
    sound: str
    def __init__(self, id: _Optional[str] = ..., hour: _Optional[int] = ..., minute: _Optional[int] = ..., days: _Optional[int] = ..., date: _Optional[_Union[Date, _Mapping]] = ..., label: _Optional[str] = ..., sound: _Optional[str] = ...) -> None: ...

class Date(_message.Message):
    __slots__ = ("year", "month", "day")
    YEAR_FIELD_NUMBER: _ClassVar[int]
    MONTH_FIELD_NUMBER: _ClassVar[int]
    DAY_FIELD_NUMBER: _ClassVar[int]
    year: int
    month: int
    day: int
    def __init__(self, year: _Optional[int] = ..., month: _Optional[int] = ..., day: _Optional[int] = ...) -> None: ...

class SoundList(_message.Message):
    __slots__ = ("names",)
    NAMES_FIELD_NUMBER: _ClassVar[int]
    names: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, names: _Optional[_Iterable[str]] = ...) -> None: ...

class NowPlaying(_message.Message):
    __slots__ = ("left", "right")
    LEFT_FIELD_NUMBER: _ClassVar[int]
    RIGHT_FIELD_NUMBER: _ClassVar[int]
    left: Listener
    right: Listener
    def __init__(self, left: _Optional[_Union[Listener, _Mapping]] = ..., right: _Optional[_Union[Listener, _Mapping]] = ...) -> None: ...

class Listener(_message.Message):
    __slots__ = ("status", "title", "artist", "art_id")
    class Status(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        OFF: _ClassVar[Listener.Status]
        PLAYING: _ClassVar[Listener.Status]
        PAUSED: _ClassVar[Listener.Status]
    OFF: Listener.Status
    PLAYING: Listener.Status
    PAUSED: Listener.Status
    STATUS_FIELD_NUMBER: _ClassVar[int]
    TITLE_FIELD_NUMBER: _ClassVar[int]
    ARTIST_FIELD_NUMBER: _ClassVar[int]
    ART_ID_FIELD_NUMBER: _ClassVar[int]
    status: Listener.Status
    title: str
    artist: str
    art_id: str
    def __init__(self, status: _Optional[_Union[Listener.Status, str]] = ..., title: _Optional[str] = ..., artist: _Optional[str] = ..., art_id: _Optional[str] = ...) -> None: ...

class FaceRef(_message.Message):
    __slots__ = ("id", "rev")
    ID_FIELD_NUMBER: _ClassVar[int]
    REV_FIELD_NUMBER: _ClassVar[int]
    id: str
    rev: str
    def __init__(self, id: _Optional[str] = ..., rev: _Optional[str] = ...) -> None: ...

class SpriteRef(_message.Message):
    __slots__ = ("hash", "w", "h", "size")
    HASH_FIELD_NUMBER: _ClassVar[int]
    W_FIELD_NUMBER: _ClassVar[int]
    H_FIELD_NUMBER: _ClassVar[int]
    SIZE_FIELD_NUMBER: _ClassVar[int]
    hash: str
    w: int
    h: int
    size: int
    def __init__(self, hash: _Optional[str] = ..., w: _Optional[int] = ..., h: _Optional[int] = ..., size: _Optional[int] = ...) -> None: ...

class TextSlot(_message.Message):
    __slots__ = ("x", "y", "size", "color", "has_shadow", "shadow", "align", "format")
    class Align(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        LEFT: _ClassVar[TextSlot.Align]
        CENTER: _ClassVar[TextSlot.Align]
        RIGHT: _ClassVar[TextSlot.Align]
    LEFT: TextSlot.Align
    CENTER: TextSlot.Align
    RIGHT: TextSlot.Align
    class DateFormat(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        LONG: _ClassVar[TextSlot.DateFormat]
        SHORT: _ClassVar[TextSlot.DateFormat]
        DAY_MONTH: _ClassVar[TextSlot.DateFormat]
        WEEKDAY: _ClassVar[TextSlot.DateFormat]
        NUMERIC: _ClassVar[TextSlot.DateFormat]
    LONG: TextSlot.DateFormat
    SHORT: TextSlot.DateFormat
    DAY_MONTH: TextSlot.DateFormat
    WEEKDAY: TextSlot.DateFormat
    NUMERIC: TextSlot.DateFormat
    X_FIELD_NUMBER: _ClassVar[int]
    Y_FIELD_NUMBER: _ClassVar[int]
    SIZE_FIELD_NUMBER: _ClassVar[int]
    COLOR_FIELD_NUMBER: _ClassVar[int]
    HAS_SHADOW_FIELD_NUMBER: _ClassVar[int]
    SHADOW_FIELD_NUMBER: _ClassVar[int]
    ALIGN_FIELD_NUMBER: _ClassVar[int]
    FORMAT_FIELD_NUMBER: _ClassVar[int]
    x: int
    y: int
    size: int
    color: int
    has_shadow: bool
    shadow: int
    align: TextSlot.Align
    format: TextSlot.DateFormat
    def __init__(self, x: _Optional[int] = ..., y: _Optional[int] = ..., size: _Optional[int] = ..., color: _Optional[int] = ..., has_shadow: _Optional[bool] = ..., shadow: _Optional[int] = ..., align: _Optional[_Union[TextSlot.Align, str]] = ..., format: _Optional[_Union[TextSlot.DateFormat, str]] = ...) -> None: ...

class Keyframe(_message.Message):
    __slots__ = ("at", "pct")
    AT_FIELD_NUMBER: _ClassVar[int]
    PCT_FIELD_NUMBER: _ClassVar[int]
    at: int
    pct: int
    def __init__(self, at: _Optional[int] = ..., pct: _Optional[int] = ...) -> None: ...

class Motion(_message.Message):
    __slots__ = ("type", "period_ms", "phase_ms", "amp", "dx", "dy", "loop", "keys", "on_ms", "min_pct", "max_pct", "frames", "fps")
    class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        NONE: _ClassVar[Motion.Type]
        BOB: _ClassVar[Motion.Type]
        SWAY: _ClassVar[Motion.Type]
        PATH: _ClassVar[Motion.Type]
        FADE: _ClassVar[Motion.Type]
        BLINK: _ClassVar[Motion.Type]
        PULSE: _ClassVar[Motion.Type]
        FRAMES: _ClassVar[Motion.Type]
    NONE: Motion.Type
    BOB: Motion.Type
    SWAY: Motion.Type
    PATH: Motion.Type
    FADE: Motion.Type
    BLINK: Motion.Type
    PULSE: Motion.Type
    FRAMES: Motion.Type
    TYPE_FIELD_NUMBER: _ClassVar[int]
    PERIOD_MS_FIELD_NUMBER: _ClassVar[int]
    PHASE_MS_FIELD_NUMBER: _ClassVar[int]
    AMP_FIELD_NUMBER: _ClassVar[int]
    DX_FIELD_NUMBER: _ClassVar[int]
    DY_FIELD_NUMBER: _ClassVar[int]
    LOOP_FIELD_NUMBER: _ClassVar[int]
    KEYS_FIELD_NUMBER: _ClassVar[int]
    ON_MS_FIELD_NUMBER: _ClassVar[int]
    MIN_PCT_FIELD_NUMBER: _ClassVar[int]
    MAX_PCT_FIELD_NUMBER: _ClassVar[int]
    FRAMES_FIELD_NUMBER: _ClassVar[int]
    FPS_FIELD_NUMBER: _ClassVar[int]
    type: Motion.Type
    period_ms: int
    phase_ms: int
    amp: int
    dx: int
    dy: int
    loop: bool
    keys: _containers.RepeatedCompositeFieldContainer[Keyframe]
    on_ms: int
    min_pct: int
    max_pct: int
    frames: int
    fps: int
    def __init__(self, type: _Optional[_Union[Motion.Type, str]] = ..., period_ms: _Optional[int] = ..., phase_ms: _Optional[int] = ..., amp: _Optional[int] = ..., dx: _Optional[int] = ..., dy: _Optional[int] = ..., loop: _Optional[bool] = ..., keys: _Optional[_Iterable[_Union[Keyframe, _Mapping]]] = ..., on_ms: _Optional[int] = ..., min_pct: _Optional[int] = ..., max_pct: _Optional[int] = ..., frames: _Optional[int] = ..., fps: _Optional[int] = ...) -> None: ...

class Layer(_message.Message):
    __slots__ = ("sprite", "x", "y", "copies", "stagger_x", "stagger_y", "stagger_phase_ms", "motion", "scale_pct", "scatter", "scatter_area", "scatter_seed", "scatter_period_ms")
    SPRITE_FIELD_NUMBER: _ClassVar[int]
    X_FIELD_NUMBER: _ClassVar[int]
    Y_FIELD_NUMBER: _ClassVar[int]
    COPIES_FIELD_NUMBER: _ClassVar[int]
    STAGGER_X_FIELD_NUMBER: _ClassVar[int]
    STAGGER_Y_FIELD_NUMBER: _ClassVar[int]
    STAGGER_PHASE_MS_FIELD_NUMBER: _ClassVar[int]
    MOTION_FIELD_NUMBER: _ClassVar[int]
    SCALE_PCT_FIELD_NUMBER: _ClassVar[int]
    SCATTER_FIELD_NUMBER: _ClassVar[int]
    SCATTER_AREA_FIELD_NUMBER: _ClassVar[int]
    SCATTER_SEED_FIELD_NUMBER: _ClassVar[int]
    SCATTER_PERIOD_MS_FIELD_NUMBER: _ClassVar[int]
    sprite: SpriteRef
    x: int
    y: int
    copies: int
    stagger_x: int
    stagger_y: int
    stagger_phase_ms: int
    motion: _containers.RepeatedCompositeFieldContainer[Motion]
    scale_pct: int
    scatter: bool
    scatter_area: Rect
    scatter_seed: int
    scatter_period_ms: int
    def __init__(self, sprite: _Optional[_Union[SpriteRef, _Mapping]] = ..., x: _Optional[int] = ..., y: _Optional[int] = ..., copies: _Optional[int] = ..., stagger_x: _Optional[int] = ..., stagger_y: _Optional[int] = ..., stagger_phase_ms: _Optional[int] = ..., motion: _Optional[_Iterable[_Union[Motion, _Mapping]]] = ..., scale_pct: _Optional[int] = ..., scatter: _Optional[bool] = ..., scatter_area: _Optional[_Union[Rect, _Mapping]] = ..., scatter_seed: _Optional[int] = ..., scatter_period_ms: _Optional[int] = ...) -> None: ...

class Rect(_message.Message):
    __slots__ = ("x", "y", "w", "h")
    X_FIELD_NUMBER: _ClassVar[int]
    Y_FIELD_NUMBER: _ClassVar[int]
    W_FIELD_NUMBER: _ClassVar[int]
    H_FIELD_NUMBER: _ClassVar[int]
    x: int
    y: int
    w: int
    h: int
    def __init__(self, x: _Optional[int] = ..., y: _Optional[int] = ..., w: _Optional[int] = ..., h: _Optional[int] = ...) -> None: ...

class Emitter(_message.Message):
    __slots__ = ("kind", "sprites", "count", "area", "speed_min", "speed_max", "sway", "period_ms", "phase_ms", "front", "seed")
    class Kind(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        FALL: _ClassVar[Emitter.Kind]
        RISE: _ClassVar[Emitter.Kind]
        TWINKLE: _ClassVar[Emitter.Kind]
    FALL: Emitter.Kind
    RISE: Emitter.Kind
    TWINKLE: Emitter.Kind
    KIND_FIELD_NUMBER: _ClassVar[int]
    SPRITES_FIELD_NUMBER: _ClassVar[int]
    COUNT_FIELD_NUMBER: _ClassVar[int]
    AREA_FIELD_NUMBER: _ClassVar[int]
    SPEED_MIN_FIELD_NUMBER: _ClassVar[int]
    SPEED_MAX_FIELD_NUMBER: _ClassVar[int]
    SWAY_FIELD_NUMBER: _ClassVar[int]
    PERIOD_MS_FIELD_NUMBER: _ClassVar[int]
    PHASE_MS_FIELD_NUMBER: _ClassVar[int]
    FRONT_FIELD_NUMBER: _ClassVar[int]
    SEED_FIELD_NUMBER: _ClassVar[int]
    kind: Emitter.Kind
    sprites: _containers.RepeatedCompositeFieldContainer[SpriteRef]
    count: int
    area: Rect
    speed_min: int
    speed_max: int
    sway: int
    period_ms: int
    phase_ms: int
    front: bool
    seed: int
    def __init__(self, kind: _Optional[_Union[Emitter.Kind, str]] = ..., sprites: _Optional[_Iterable[_Union[SpriteRef, _Mapping]]] = ..., count: _Optional[int] = ..., area: _Optional[_Union[Rect, _Mapping]] = ..., speed_min: _Optional[int] = ..., speed_max: _Optional[int] = ..., sway: _Optional[int] = ..., period_ms: _Optional[int] = ..., phase_ms: _Optional[int] = ..., front: _Optional[bool] = ..., seed: _Optional[int] = ...) -> None: ...

class When(_message.Message):
    __slots__ = ("hour_from", "hour_to", "months", "days", "dates")
    HOUR_FROM_FIELD_NUMBER: _ClassVar[int]
    HOUR_TO_FIELD_NUMBER: _ClassVar[int]
    MONTHS_FIELD_NUMBER: _ClassVar[int]
    DAYS_FIELD_NUMBER: _ClassVar[int]
    DATES_FIELD_NUMBER: _ClassVar[int]
    hour_from: int
    hour_to: int
    months: int
    days: int
    dates: _containers.RepeatedCompositeFieldContainer[Date]
    def __init__(self, hour_from: _Optional[int] = ..., hour_to: _Optional[int] = ..., months: _Optional[int] = ..., days: _Optional[int] = ..., dates: _Optional[_Iterable[_Union[Date, _Mapping]]] = ...) -> None: ...

class NowPlayingSlot(_message.Message):
    __slots__ = ("show", "x", "y", "w", "has_bg", "bg", "bg_pct", "title", "artist")
    SHOW_FIELD_NUMBER: _ClassVar[int]
    X_FIELD_NUMBER: _ClassVar[int]
    Y_FIELD_NUMBER: _ClassVar[int]
    W_FIELD_NUMBER: _ClassVar[int]
    HAS_BG_FIELD_NUMBER: _ClassVar[int]
    BG_FIELD_NUMBER: _ClassVar[int]
    BG_PCT_FIELD_NUMBER: _ClassVar[int]
    TITLE_FIELD_NUMBER: _ClassVar[int]
    ARTIST_FIELD_NUMBER: _ClassVar[int]
    show: bool
    x: int
    y: int
    w: int
    has_bg: bool
    bg: int
    bg_pct: int
    title: int
    artist: int
    def __init__(self, show: _Optional[bool] = ..., x: _Optional[int] = ..., y: _Optional[int] = ..., w: _Optional[int] = ..., has_bg: _Optional[bool] = ..., bg: _Optional[int] = ..., bg_pct: _Optional[int] = ..., title: _Optional[int] = ..., artist: _Optional[int] = ...) -> None: ...

class FaceWeatherVariant(_message.Message):
    __slots__ = ("background", "layers", "emitters", "text_color", "override_text")
    BACKGROUND_FIELD_NUMBER: _ClassVar[int]
    LAYERS_FIELD_NUMBER: _ClassVar[int]
    EMITTERS_FIELD_NUMBER: _ClassVar[int]
    TEXT_COLOR_FIELD_NUMBER: _ClassVar[int]
    OVERRIDE_TEXT_FIELD_NUMBER: _ClassVar[int]
    background: SpriteRef
    layers: _containers.RepeatedCompositeFieldContainer[Layer]
    emitters: _containers.RepeatedCompositeFieldContainer[Emitter]
    text_color: int
    override_text: bool
    def __init__(self, background: _Optional[_Union[SpriteRef, _Mapping]] = ..., layers: _Optional[_Iterable[_Union[Layer, _Mapping]]] = ..., emitters: _Optional[_Iterable[_Union[Emitter, _Mapping]]] = ..., text_color: _Optional[int] = ..., override_text: _Optional[bool] = ...) -> None: ...

class Face(_message.Message):
    __slots__ = ("id", "rev", "background", "time", "date", "icon", "layers", "emitters", "when", "nowplaying", "cloudy", "rain")
    ID_FIELD_NUMBER: _ClassVar[int]
    REV_FIELD_NUMBER: _ClassVar[int]
    BACKGROUND_FIELD_NUMBER: _ClassVar[int]
    TIME_FIELD_NUMBER: _ClassVar[int]
    DATE_FIELD_NUMBER: _ClassVar[int]
    ICON_FIELD_NUMBER: _ClassVar[int]
    LAYERS_FIELD_NUMBER: _ClassVar[int]
    EMITTERS_FIELD_NUMBER: _ClassVar[int]
    WHEN_FIELD_NUMBER: _ClassVar[int]
    NOWPLAYING_FIELD_NUMBER: _ClassVar[int]
    CLOUDY_FIELD_NUMBER: _ClassVar[int]
    RAIN_FIELD_NUMBER: _ClassVar[int]
    id: str
    rev: str
    background: SpriteRef
    time: TextSlot
    date: TextSlot
    icon: Layer
    layers: _containers.RepeatedCompositeFieldContainer[Layer]
    emitters: _containers.RepeatedCompositeFieldContainer[Emitter]
    when: When
    nowplaying: NowPlayingSlot
    cloudy: FaceWeatherVariant
    rain: FaceWeatherVariant
    def __init__(self, id: _Optional[str] = ..., rev: _Optional[str] = ..., background: _Optional[_Union[SpriteRef, _Mapping]] = ..., time: _Optional[_Union[TextSlot, _Mapping]] = ..., date: _Optional[_Union[TextSlot, _Mapping]] = ..., icon: _Optional[_Union[Layer, _Mapping]] = ..., layers: _Optional[_Iterable[_Union[Layer, _Mapping]]] = ..., emitters: _Optional[_Iterable[_Union[Emitter, _Mapping]]] = ..., when: _Optional[_Union[When, _Mapping]] = ..., nowplaying: _Optional[_Union[NowPlayingSlot, _Mapping]] = ..., cloudy: _Optional[_Union[FaceWeatherVariant, _Mapping]] = ..., rain: _Optional[_Union[FaceWeatherVariant, _Mapping]] = ...) -> None: ...

class Weather(_message.Message):
    __slots__ = ("available", "temperature_c", "code", "is_day", "observed_at", "city")
    AVAILABLE_FIELD_NUMBER: _ClassVar[int]
    TEMPERATURE_C_FIELD_NUMBER: _ClassVar[int]
    CODE_FIELD_NUMBER: _ClassVar[int]
    IS_DAY_FIELD_NUMBER: _ClassVar[int]
    OBSERVED_AT_FIELD_NUMBER: _ClassVar[int]
    CITY_FIELD_NUMBER: _ClassVar[int]
    available: bool
    temperature_c: int
    code: int
    is_day: bool
    observed_at: int
    city: str
    def __init__(self, available: _Optional[bool] = ..., temperature_c: _Optional[int] = ..., code: _Optional[int] = ..., is_day: _Optional[bool] = ..., observed_at: _Optional[int] = ..., city: _Optional[str] = ...) -> None: ...

class Management(_message.Message):
    __slots__ = ("diagnostic_interval", "release", "size")
    DIAGNOSTIC_INTERVAL_FIELD_NUMBER: _ClassVar[int]
    RELEASE_FIELD_NUMBER: _ClassVar[int]
    SIZE_FIELD_NUMBER: _ClassVar[int]
    diagnostic_interval: int
    release: str
    size: int
    def __init__(self, diagnostic_interval: _Optional[int] = ..., release: _Optional[str] = ..., size: _Optional[int] = ...) -> None: ...
