"""Shared volume validation for the authenticated admin and device APIs."""
from model import Invalid

DEFAULT = {'alarm': 18, 'video': 18}

def clean(value):
    if not isinstance(value, dict):
        raise Invalid('Expected alarm and video volume levels.')
    alarm, video = value.get('alarm'), value.get('video')
    if type(alarm) is not int or type(video) is not int or not (0 <= alarm <= 21 and 0 <= video <= 21):
        raise Invalid('Volume must be an integer from 0 to 21.')
    return {'alarm': alarm, 'video': video}
