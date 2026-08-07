from dataclasses import FrozenInstanceError, fields
from datetime import datetime, timezone
from decimal import Decimal

import pytest

from polyvenue.events import (
    BookSnapshot,
    Event,
    FeedGap,
    Fill,
    OrderStatus,
    OrderUpdate,
    PriceChange,
    Side,
    TickSizeChange,
    Trade,
)

T_WALL = datetime(2026, 7, 17, tzinfo=timezone.utc)

ALL_EVENTS = [
    BookSnapshot(t_mono=1.0, t_wall=T_WALL, condition_id="0xcond", asset_id="tok",
                 bids=((Decimal("0.4"), Decimal("10")),), asks=((Decimal("0.6"), Decimal("20")),)),
    PriceChange(t_mono=1.0, t_wall=T_WALL, condition_id="0xcond", asset_id="tok",
                side=Side.BUY, price=Decimal("0.4"), size=Decimal("10")),
    TickSizeChange(t_mono=1.0, t_wall=T_WALL, condition_id="0xcond", asset_id="tok",
                   tick_size=Decimal("0.001")),
    Trade(t_mono=1.0, t_wall=T_WALL, condition_id="0xcond", asset_id="tok",
          side=Side.SELL, price=Decimal("0.4"), size=Decimal("3")),
    FeedGap(t_mono=1.0, t_wall=T_WALL, source="market-ws"),
    OrderUpdate(t_mono=1.0, t_wall=T_WALL, order_id="0xabc", asset_id="tok", side=Side.BUY,
                price=Decimal("0.93"), size=Decimal("5"), size_matched=Decimal("0"),
                status=OrderStatus.LIVE),
    Fill(t_mono=1.0, t_wall=T_WALL, order_id="0xabc", trade_id="0xdef", asset_id="tok",
         side=Side.BUY, price=Decimal("0.93"), size=Decimal("5")),
]


@pytest.mark.parametrize("event", ALL_EVENTS, ids=lambda e: type(e).__name__)
def test_every_event_carries_both_clocks(event):
    assert isinstance(event, Event)
    assert event.t_mono == 1.0
    assert event.t_wall == T_WALL


@pytest.mark.parametrize("event", ALL_EVENTS, ids=lambda e: type(e).__name__)
def test_events_are_frozen(event):
    """Pure strategy functions (D6) must not be able to mutate what they are fed."""
    field_name = fields(event)[-1].name
    with pytest.raises(FrozenInstanceError):
        setattr(event, field_name, None)


@pytest.mark.parametrize("event", ALL_EVENTS, ids=lambda e: type(e).__name__)
def test_events_use_slots(event):
    """slots keeps ~90k events per instance cheap and rejects typo'd attributes."""
    assert not hasattr(event, "__dict__")
    with pytest.raises(AttributeError):
        object.__setattr__(event, "typo", 1)


def test_enum_values_match_the_wire():
    """These strings go to and come from the venue verbatim; renaming them is a bug."""
    assert [s.value for s in Side] == ["BUY", "SELL"]
    assert [s.value for s in OrderStatus] == ["LIVE", "MATCHED", "CANCELED"]
    assert Side("BUY") is Side.BUY
    assert OrderStatus("MATCHED") is OrderStatus.MATCHED


def test_events_compare_by_value():
    a = PriceChange(t_mono=1.0, t_wall=T_WALL, condition_id="0xcond", asset_id="tok",
                    side=Side.BUY, price=Decimal("0.4"), size=Decimal("10"))
    b = PriceChange(t_mono=1.0, t_wall=T_WALL, condition_id="0xcond", asset_id="tok",
                    side=Side.BUY, price=Decimal("0.4"), size=Decimal("10"))
    assert a == b
    assert a != PriceChange(t_mono=1.0, t_wall=T_WALL, condition_id="0xcond", asset_id="tok",
                            side=Side.SELL, price=Decimal("0.4"), size=Decimal("10"))


def test_book_snapshot_levels_are_immutable():
    snap = ALL_EVENTS[0]
    assert isinstance(snap.bids, tuple)
    with pytest.raises(TypeError):
        snap.bids[0] = (Decimal("0.5"), Decimal("1"))


def test_book_snapshot_tick_size_optional():
    assert ALL_EVENTS[0].tick_size is None


def test_feed_gap_defaults_to_unknown_span():
    gap = FeedGap(t_mono=1.0, t_wall=T_WALL, source="user-ws")
    assert gap.since_t_mono is None
    assert gap.reason == ""


def test_fill_defaults_fee_rate_to_none():
    """Maker entries carry no fee_rate_bps, so the rebate cannot come from the stream (D12)."""
    fill = Fill(t_mono=1.0, t_wall=T_WALL, order_id="0xabc", trade_id="0xdef", asset_id="tok",
                side=Side.BUY, price=Decimal("0.93"), size=Decimal("5"))
    assert fill.fee_rate_bps is None


def test_order_update_size_matched_is_cumulative():
    """size_matched is the venue's running total, not a per-event delta."""
    first = OrderUpdate(t_mono=1.0, t_wall=T_WALL, order_id="0xabc", asset_id="tok", side=Side.BUY,
                        price=Decimal("0.93"), size=Decimal("5"), size_matched=Decimal("2"),
                        status=OrderStatus.LIVE)
    last = OrderUpdate(t_mono=2.0, t_wall=T_WALL, order_id="0xabc", asset_id="tok", side=Side.BUY,
                       price=Decimal("0.93"), size=Decimal("5"), size_matched=Decimal("5"),
                       status=OrderStatus.MATCHED)
    assert last.size_matched == last.size
    assert last.size_matched > first.size_matched
