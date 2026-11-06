from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DeviceState(_message.Message):
    __slots__ = ("version", "note", "photo", "alarms", "nowplaying")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    NOTE_FIELD_NUMBER: _ClassVar[int]
    PHOTO_FIELD_NUMBER: _ClassVar[int]
    ALARMS_FIELD_NUMBER: _ClassVar[int]
    NOWPLAYING_FIELD_NUMBER: _ClassVar[int]
    version: int
    note: Note
    photo: Photo
    alarms: _containers.RepeatedCompositeFieldContainer[Alarm]
    nowplaying: NowPlaying
    def __init__(self, version: _Optional[int] = ..., note: _Optional[_Union[Note, _Mapping]] = ..., photo: _Optional[_Union[Photo, _Mapping]] = ..., alarms: _Optional[_Iterable[_Union[Alarm, _Mapping]]] = ..., nowplaying: _Optional[_Union[NowPlaying, _Mapping]] = ...) -> None: ...

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
