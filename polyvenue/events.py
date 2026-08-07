"""The event vocabulary the strategy sees.

Both sources emit these: the live SDK adapter and the tape reader. The strategy
never sees an SDK object, which is what makes a sim run comparable to a live run.
"""
from dataclasses import dataclass
from datetime import datetime
from decimal import Decimal
from enum import Enum
from typing import Optional, Union


class Side(str, Enum):
    BUY = "BUY"
    SELL = "SELL"


class OrderStatus(str, Enum):
    LIVE = "LIVE"
    MATCHED = "MATCHED"
    CANCELED = "CANCELED"


@dataclass(frozen=True, slots=True)
class Event:
    """Base for every event.

    t_mono orders events and drives latency math; it is the only clock safe for
    durations. t_wall is for correlating with outside data (spot, oracle) and can
    jump backwards.
    """
    t_mono: float
    t_wall: datetime


# --------------------------------------------------------------------------- #
# Market data
# --------------------------------------------------------------------------- #

@dataclass(frozen=True, slots=True)
class BookSnapshot(Event):
    """Replaces one token's book. Sent on seed, reconnect, and ~2/sec by the venue."""
    condition_id: str
    asset_id: str
    bids: tuple[tuple[Decimal, Decimal], ...]   # (price, size), best-first
    asks: tuple[tuple[Decimal, Decimal], ...]
    tick_size: Optional[Decimal] = None


@dataclass(frozen=True, slots=True)
class PriceChange(Event):
    """Sets the total size at one price level. size == 0 removes the level."""
    condition_id: str
    asset_id: str
    side: Side
    price: Decimal
    size: Decimal


@dataclass(frozen=True, slots=True)
class TickSizeChange(Event):
    condition_id: str
    asset_id: str
    tick_size: Decimal


@dataclass(frozen=True, slots=True)
class Trade(Event):
    """A print on the tape. Drives queue-position attribution, not our own fills."""
    condition_id: str
    asset_id: str
    side: Side          # the taker's side
    price: Decimal
    size: Decimal


@dataclass(frozen=True, slots=True)
class FeedGap(Event):
    """Marks a span where the feed was blind: disconnect, stall, or dropped frame.

    First-class because the strategy must react to not-knowing. An absence cannot
    be delivered, so it is delivered as an event.
    """
    source: str                        # which stream went quiet
    since_t_mono: Optional[float] = None  # start of the blind span; None if unknown
    reason: str = ""


# --------------------------------------------------------------------------- #
# Our orders
# --------------------------------------------------------------------------- #

@dataclass(frozen=True, slots=True)
class OrderUpdate(Event):
    """Status of one of our orders. size_matched is cumulative, not a delta."""
    order_id: str
    asset_id: str
    side: Side
    price: Decimal
    size: Decimal
    size_matched: Decimal
    status: OrderStatus


@dataclass(frozen=True, slots=True)
class Fill(Event):
    """One of our orders traded, in OUR terms.

    Every field describes our leg. The venue's trade payload puts the
    counterparty's complement leg at the top level, so the adapter must read
    `maker_orders[i]` matched on order_id and never the envelope.

    fee_rate_bps is None on maker entries -- the stream carries no rebate data.
    """
    order_id: str
    trade_id: str
    asset_id: str
    side: Side
    price: Decimal
    size: Decimal
    fee_rate_bps: Optional[int] = None


MarketEvent = Union[BookSnapshot, PriceChange, TickSizeChange, Trade, FeedGap]
UserEvent = Union[OrderUpdate, Fill]
AnyEvent = Union[MarketEvent, UserEvent]
