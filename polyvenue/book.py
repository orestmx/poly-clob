"""Reconstructs L2 order books from a seed + delta event stream."""
import logging
from bisect import bisect_left, insort
from decimal import Decimal
from typing import Iterator, Optional

logger = logging.getLogger(__name__)

_BOOK_EVENT_TYPES = frozenset({"seed", "book", "price_change", "tick_size_change"})


class Book:
    """One token's L2 order book. bids/asks map price -> total size.

    Level keys are Decimal, never float: binary drift would orphan a level and a
    later delete would miss it.
    """

    def __init__(self, tick_size: Decimal = Decimal("0.01")) -> None:
        self.bids: dict[Decimal, Decimal] = {}
        self.asks: dict[Decimal, Decimal] = {}
        self.tick_size = tick_size
        # Ascending price order per side; bids are reversed on the way out.
        self._bid_order: list[Decimal] = []
        self._ask_order: list[Decimal] = []
        # Float caches, written on level change. _fpx is shared by both sides and
        # bounded by ~1/tick_size entries, so it is never evicted.
        self._fpx: dict[Decimal, float] = {}
        self._bid_fsz: dict[Decimal, float] = {}
        self._ask_fsz: dict[Decimal, float] = {}

    def _cache_price(self, price: Decimal) -> None:
        if price not in self._fpx:
            self._fpx[price] = float(price)

    def apply_seed(self, bids: list[dict], asks: list[dict], tick_size: Optional[Decimal] = None) -> None:
        """Replaces the whole book, from a seed marker or a `book` snapshot."""
        self.bids = {lv["price"]: lv["size"] for lv in bids if lv.get("price") is not None}
        self.asks = {lv["price"]: lv["size"] for lv in asks if lv.get("price") is not None}
        self._bid_order = sorted(self.bids)
        self._ask_order = sorted(self.asks)
        for price in self._bid_order:
            self._cache_price(price)
        for price in self._ask_order:
            self._cache_price(price)
        self._bid_fsz = {p: float(s) for p, s in self.bids.items()}
        self._ask_fsz = {p: float(s) for p, s in self.asks.items()}
        if tick_size is not None:
            self.tick_size = tick_size

    def apply_price_change(self, changes: list[dict]) -> None:
        """Sets size at each price level; drops the level when size is 0."""
        for change in changes:
            price = change.get("price")
            size = change.get("size")
            side = change.get("side")
            if price is None or size is None or side not in ("BUY", "SELL"):
                continue
            if side == "BUY":
                book_side, order, fsz = self.bids, self._bid_order, self._bid_fsz
            else:
                book_side, order, fsz = self.asks, self._ask_order, self._ask_fsz

            if size == 0:
                if book_side.pop(price, None) is not None:
                    idx = bisect_left(order, price)
                    if idx < len(order) and order[idx] == price:
                        del order[idx]
                    fsz.pop(price, None)
            else:
                if price not in book_side:
                    insort(order, price)
                    self._cache_price(price)
                book_side[price] = size
                fsz[price] = float(size)

    def apply_tick_size(self, new_tick_size: Decimal) -> None:
        self.tick_size = new_tick_size

    def snapshot(self) -> dict:
        """Sorts bids/asks, best-first: bids descending, asks ascending."""
        bids = self.bids
        asks = self.asks
        return {
            "bids": [{"price": p, "size": bids[p]} for p in reversed(self._bid_order)],
            "asks": [{"price": p, "size": asks[p]} for p in self._ask_order],
        }

    def snapshot_floats(self) -> dict:
        """Returns `snapshot()` with prices and sizes converted to float64."""
        fpx = self._fpx
        bid_fsz = self._bid_fsz
        ask_fsz = self._ask_fsz
        return {
            "bids": [{"price": fpx[p], "size": bid_fsz[p]} for p in reversed(self._bid_order)],
            "asks": [{"price": fpx[p], "size": ask_fsz[p]} for p in self._ask_order],
        }


def replay_market(records: Iterator[dict]) -> Iterator[dict]:
    """Replays one market's event stream into BookRow dicts, one per book event.

    Up and Down tokens interleave in the stream; a book is kept per asset_id.
    """
    books: dict[str, Book] = {}

    for record in records:
        event_type = record.get("event_type")
        if event_type not in _BOOK_EVENT_TYPES:
            continue
        asset_id = record.get("asset_id")
        if asset_id is None:
            continue

        book = books.setdefault(asset_id, Book())

        if event_type == "seed":
            book.apply_seed(record.get("bids", []), record.get("asks", []), record.get("tick_size"))
        elif event_type == "book":
            book.apply_seed(record.get("bids", []), record.get("asks", []))
        elif event_type == "price_change":
            book.apply_price_change([{
                "price": record.get("price"),
                "size": record.get("size"),
                "side": record.get("side"),
            }])
        elif event_type == "tick_size_change":
            new_tick = record.get("new_tick_size")
            if new_tick is not None:
                book.apply_tick_size(new_tick)

        snap = book.snapshot()
        yield {
            "t_mono": record["t_mono"],
            "t_wall": record["t_wall"],
            "condition_id": record.get("condition_id"),
            "asset_id": asset_id,
            "token_side": record.get("token_side"),
            "event_type": event_type,
            "bids": snap["bids"],
            "asks": snap["asks"],
        }
