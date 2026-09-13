"""The day's costume for the device mascots (Clawd and the Kiro ghost).

Sent as {"cos": n} with n matching SPLASH_COSTUME_* in firmware/src/splash.h.
A Vasco match day wins over any holiday: on match days they wear the shirt.
"""

import datetime

NONE, VASCO, NATAL, CARNAVAL, HALLOWEEN = range(5)


def easter(year: int) -> datetime.date:
    """Gregorian Easter Sunday (anonymous algorithm)."""
    a, b, c = year % 19, year // 100, year % 100
    d, e = b // 4, b % 4
    f = (b + 8) // 25
    g = (b - f + 1) // 3
    h = (19 * a + b - d - g + 15) % 30
    i, k = c // 4, c % 4
    l = (32 + 2 * e + 2 * i - h - k) % 7
    m = (a + 11 * h + 22 * l) // 451
    month = (h + l - 7 * m + 114) // 31
    day = (h + l - 7 * m + 114) % 31 + 1
    return datetime.date(year, month, day)


def carnaval_days(year: int) -> tuple[datetime.date, datetime.date]:
    """Carnaval Saturday through Carnaval Tuesday (Easter - 50 .. Easter - 47 days)."""
    e = easter(year)
    return e - datetime.timedelta(days=50), e - datetime.timedelta(days=47)


def costume_for(day: datetime.date, match_day: bool) -> int:
    if match_day:
        return VASCO
    if (day.month, day.day) in ((12, 24), (12, 25)):
        return NATAL
    first, last = carnaval_days(day.year)
    if first <= day <= last:
        return CARNAVAL
    if (day.month, day.day) == (10, 31):
        return HALLOWEEN
    return NONE
