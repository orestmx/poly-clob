"""Domain arithmetic: representation, the grid, complement, lots.

The price space is 1001 values wide, so most of these are exhaustive rather than
sampled -- there is no reason to spot-check a space you can enumerate in a millisecond.
"""
from decimal import Decimal

import pytest

from polyvenue.domain import (
    MILS_PER_UNIT,
    ceil_to_grid,
    complement,
    floor_to_grid,
    is_on_grid,
    is_tradeable,
    lots_to_shares,
    shares_to_lots,
    tick_to_mils,
    to_decimal,
    to_mils,
)

ALL_PRICES = range(MILS_PER_UNIT + 1)
LIVE_TICKS = [10, 1]            # the two regimes seen live: 0.01 and 0.001
MIN_ORDER_SIZE = Decimal(5)     # every 5m and 15m market


# -- representation ---------------------------------------------------------

def test_mils_round_trip_is_exact():
    for mils in ALL_PRICES:
        assert to_mils(to_decimal(mils)) == mils


@pytest.mark.parametrize("price, expected", [
    ("0.93", 930),
    ("0.001", 1),
    ("0.9295", 930),        # .5 rounds up
    ("0.9294", 929),
    ("0", 0),
    ("1", MILS_PER_UNIT),
])
def test_to_mils_rounds_to_nearest(price, expected):
    assert to_mils(Decimal(price)) == expected


def test_off_grid_arriving_price_converts_without_raising():
    # From the captured fill fixture: the venue really does send this on a 0.01 market.
    assert to_mils(Decimal("0.0900000092763432")) == 90


@pytest.mark.parametrize("tick, expected", [("0.01", 10), ("0.001", 1)])
def test_tick_to_mils(tick, expected):
    assert tick_to_mils(Decimal(tick)) == expected


@pytest.mark.parametrize("tick", ["0.0001", "0.00001", "0"])
def test_tick_finer_than_one_mil_raises(tick):
    # Metadata, not market data: better to fail here than to divide by zero in the quoter.
    with pytest.raises(ValueError, match="finer than one mil"):
        tick_to_mils(Decimal(tick))


# -- the grid ---------------------------------------------------------------

@pytest.mark.parametrize("tick", LIVE_TICKS)
def test_grid_brackets_the_price(tick):
    for mils in ALL_PRICES:
        low, high = floor_to_grid(mils, tick), ceil_to_grid(mils, tick)
        assert low <= mils <= high
        assert is_on_grid(low, tick) and is_on_grid(high, tick)
        assert high - low <= tick


@pytest.mark.parametrize("tick", LIVE_TICKS)
def test_grid_snapping_is_idempotent(tick):
    for mils in ALL_PRICES:
        low, high = floor_to_grid(mils, tick), ceil_to_grid(mils, tick)
        assert floor_to_grid(low, tick) == low
        assert ceil_to_grid(high, tick) == high


@pytest.mark.parametrize("tick", LIVE_TICKS)
def test_on_grid_prices_are_left_alone(tick):
    for mils in range(0, MILS_PER_UNIT + 1, tick):
        assert floor_to_grid(mils, tick) == mils == ceil_to_grid(mils, tick)


def test_bid_rounds_down_and_ask_rounds_up():
    # The worked example: 0.92/0.93 book, 30-mil half spread, 0.01 tick.
    mid, tick = 925, tick_to_mils(Decimal("0.01"))
    bid, ask = floor_to_grid(mid - 30, tick), ceil_to_grid(mid + 30, tick)
    assert (bid, ask) == (890, 960)
    assert bid < ask                                  # never crossed by rounding
    assert complement(ask) == 40                      # the ask leg, as a DOWN buy


# -- complement -------------------------------------------------------------

def test_complement_is_an_involution():
    for mils in ALL_PRICES:
        assert complement(complement(mils)) == mils


def test_complement_pairs_sum_to_one():
    for mils in ALL_PRICES:
        assert mils + complement(mils) == MILS_PER_UNIT


@pytest.mark.parametrize("tick", LIVE_TICKS)
def test_complement_inverts_rounding_direction(tick):
    # Snapping an ask up in UP space is snapping it down in DOWN space. If this ever
    # stopped holding, the ask leg would land a tick off and quotes would cross silently.
    for mils in ALL_PRICES:
        assert complement(ceil_to_grid(mils, tick)) == floor_to_grid(complement(mils), tick)


def test_the_rounding_law_needs_a_tick_that_divides_the_unit():
    # Proof the test above has teeth: 3 mils does not divide 1000, and the law collapses.
    # Documents exactly which venue change would invalidate it.
    assert MILS_PER_UNIT % 3 != 0
    broken = [
        mils for mils in ALL_PRICES
        if complement(ceil_to_grid(mils, 3)) != floor_to_grid(complement(mils), 3)
    ]
    assert broken


# -- what's tradeable -------------------------------------------------------

def test_settled_outcomes_are_not_tradeable():
    assert not is_tradeable(0)
    assert not is_tradeable(MILS_PER_UNIT)
    assert is_tradeable(1)
    assert is_tradeable(MILS_PER_UNIT - 1)


def test_the_wings_can_run_out_of_room():
    # 0.985 mid + 30-mil half spread wants an ask at 1.015, which is not a price.
    assert not is_tradeable(985 + 30)


# -- size -------------------------------------------------------------------

def test_lots_round_trip():
    for lots in range(20):
        assert shares_to_lots(lots_to_shares(lots, MIN_ORDER_SIZE), MIN_ORDER_SIZE) == lots


def test_one_lot_is_the_minimum_order():
    assert lots_to_shares(1, MIN_ORDER_SIZE) == Decimal(5)


@pytest.mark.parametrize("shares, lots", [("0", 0), ("4", 0), ("5", 1), ("7", 1), ("10", 2)])
def test_shares_to_lots_rounds_down(shares, lots):
    # Documented floor: a risk check that must not understate rounds up at the call site.
    assert shares_to_lots(Decimal(shares), MIN_ORDER_SIZE) == lots


def test_sub_lot_cap_is_visible_as_zero_lots():
    # Standing rule 4: a 2-share cap under a 5-share minimum is not a cap at all, and in
    # lots that is obvious -- it is zero.
    assert shares_to_lots(Decimal(2), MIN_ORDER_SIZE) == 0


@pytest.mark.parametrize("bad", ["0", "-5"])
def test_non_positive_min_order_size_raises(bad):
    with pytest.raises(ValueError, match="must be positive"):
        lots_to_shares(1, Decimal(bad))
    with pytest.raises(ValueError, match="must be positive"):
        shares_to_lots(Decimal(10), Decimal(bad))
