import datetime

from daemon.costumes import CARNAVAL, HALLOWEEN, NATAL, NONE, VASCO, carnaval_days, costume_for, easter

D = datetime.date


def test_easter_known_dates():
    assert easter(2026) == D(2026, 4, 5)
    assert easter(2027) == D(2027, 3, 28)


def test_carnaval_saturday_to_tuesday():
    assert carnaval_days(2027) == (D(2027, 2, 6), D(2027, 2, 9))
    assert costume_for(D(2027, 2, 6), False) == CARNAVAL
    assert costume_for(D(2027, 2, 9), False) == CARNAVAL
    assert costume_for(D(2027, 2, 10), False) == NONE   # Ash Wednesday: back to normal


def test_holidays_and_match_day_priority():
    assert costume_for(D(2026, 12, 24), False) == NATAL
    assert costume_for(D(2026, 12, 25), False) == NATAL
    assert costume_for(D(2026, 12, 26), False) == NONE
    assert costume_for(D(2026, 10, 31), False) == HALLOWEEN
    assert costume_for(D(2026, 10, 31), True) == VASCO      # Vasco shirt wins on match days
    assert costume_for(D(2026, 9, 13), False) == NONE
