from decimal import Decimal

from polyvenue.book import Book, replay_market


def _level(price, size):
    return {"price": Decimal(price), "size": Decimal(size)}


def test_book_apply_seed_replaces_entire_book():
    book = Book()
    book.apply_seed([_level("0.4", "10"), _level("0.3", "5")], [_level("0.6", "20")], Decimal("0.01"))

    assert book.bids == {Decimal("0.4"): Decimal("10"), Decimal("0.3"): Decimal("5")}
    assert book.asks == {Decimal("0.6"): Decimal("20")}
    assert book.tick_size == Decimal("0.01")


def test_book_apply_price_change_updates_and_removes_levels():
    book = Book()
    book.apply_seed([_level("0.4", "10")], [_level("0.6", "20")])

    book.apply_price_change([{"price": Decimal("0.4"), "size": Decimal("15"), "side": "BUY"}])
    assert book.bids[Decimal("0.4")] == Decimal("15")

    book.apply_price_change([{"price": Decimal("0.4"), "size": Decimal("0"), "side": "BUY"}])
    assert Decimal("0.4") not in book.bids

    book.apply_price_change([{"price": Decimal("0.55"), "size": Decimal("3"), "side": "SELL"}])
    assert book.asks[Decimal("0.55")] == Decimal("3")


def test_book_apply_price_change_ignores_unknown_side():
    book = Book()
    book.apply_price_change([{"price": Decimal("0.4"), "size": Decimal("1"), "side": "WHAT"}])
    assert book.bids == {}
    assert book.asks == {}


def test_book_apply_tick_size():
    book = Book()
    book.apply_tick_size(Decimal("0.001"))
    assert book.tick_size == Decimal("0.001")


def test_book_snapshot_sorted_best_first():
    book = Book()
    book.apply_seed(
        [_level("0.3", "1"), _level("0.5", "2"), _level("0.4", "3")],
        [_level("0.7", "1"), _level("0.6", "2"), _level("0.9", "3")],
    )
    snap = book.snapshot()

    assert [lv["price"] for lv in snap["bids"]] == [Decimal("0.5"), Decimal("0.4"), Decimal("0.3")]
    assert [lv["price"] for lv in snap["asks"]] == [Decimal("0.6"), Decimal("0.7"), Decimal("0.9")]


def _grouped_event(event_type, asset_id, condition_id="0xcond", token_side="Up", t_mono=1, **fields):
    return {
        "t_mono": t_mono,
        "t_wall": "2026-07-17T00:00:00.000000Z",
        "condition_id": condition_id,
        "asset_id": asset_id,
        "token_side": token_side,
        "event_type": event_type,
        **fields,
    }


def test_replay_market_seed_then_price_change_emits_rows():
    records = iter([
        _grouped_event("seed", "tok-up", t_mono=1,
                        bids=[_level("0.4", "10")], asks=[_level("0.6", "20")], tick_size=Decimal("0.01")),
        _grouped_event("price_change", "tok-up", t_mono=2,
                        price=Decimal("0.4"), size=Decimal("50"), side="BUY"),
    ])

    rows = list(replay_market(records))

    assert len(rows) == 2
    assert rows[0]["event_type"] == "seed"
    assert rows[0]["bids"] == [{"price": Decimal("0.4"), "size": Decimal("10")}]
    assert rows[1]["event_type"] == "price_change"
    assert rows[1]["bids"] == [{"price": Decimal("0.4"), "size": Decimal("50")}]
    assert rows[1]["asks"] == [{"price": Decimal("0.6"), "size": Decimal("20")}]
    assert rows[1]["condition_id"] == "0xcond"
    assert rows[1]["asset_id"] == "tok-up"
    assert rows[1]["token_side"] == "Up"


def test_replay_market_tracks_two_tokens_independently():
    records = iter([
        _grouped_event("seed", "tok-up", token_side="Up",
                        bids=[_level("0.4", "10")], asks=[_level("0.6", "20")]),
        _grouped_event("seed", "tok-down", token_side="Down",
                        bids=[_level("0.35", "5")], asks=[_level("0.65", "8")]),
        _grouped_event("price_change", "tok-up", token_side="Up",
                        price=Decimal("0.4"), size=Decimal("99"), side="BUY"),
    ])

    rows = list(replay_market(records))

    assert len(rows) == 3
    up_row = rows[-1]
    assert up_row["asset_id"] == "tok-up"
    assert up_row["bids"] == [{"price": Decimal("0.4"), "size": Decimal("99")}]
    # the Down token's book must be untouched by the Up token's price_change
    down_seed_row = rows[1]
    assert down_seed_row["asset_id"] == "tok-down"
    assert down_seed_row["bids"] == [{"price": Decimal("0.35"), "size": Decimal("5")}]


def test_replay_market_book_event_replaces_snapshot():
    records = iter([
        _grouped_event("seed", "tok-up", bids=[_level("0.4", "10")], asks=[_level("0.6", "20")]),
        _grouped_event("book", "tok-up", bids=[_level("0.5", "1")], asks=[_level("0.55", "2")]),
    ])

    rows = list(replay_market(records))

    assert rows[-1]["bids"] == [{"price": Decimal("0.5"), "size": Decimal("1")}]
    assert rows[-1]["asks"] == [{"price": Decimal("0.55"), "size": Decimal("2")}]


def test_replay_market_tick_size_change_emits_row_without_changing_levels():
    records = iter([
        _grouped_event("seed", "tok-up", bids=[_level("0.4", "10")], asks=[_level("0.6", "20")]),
        _grouped_event("tick_size_change", "tok-up", new_tick_size=Decimal("0.001")),
    ])

    rows = list(replay_market(records))

    assert len(rows) == 2
    assert rows[-1]["event_type"] == "tick_size_change"
    assert rows[-1]["bids"] == [{"price": Decimal("0.4"), "size": Decimal("10")}]


def test_replay_market_skips_non_book_events():
    records = iter([
        _grouped_event("last_trade_price", "tok-up"),
        _grouped_event("seed", "tok-up", bids=[_level("0.4", "10")], asks=[]),
    ])

    rows = list(replay_market(records))

    assert len(rows) == 1
    assert rows[0]["event_type"] == "seed"


def test_replay_market_missing_asset_id_skipped():
    records = iter([_grouped_event("price_change", None, price=Decimal("0.4"), size=Decimal("1"), side="BUY")])
    rows = list(replay_market(records))
    assert rows == []


# ---------------------------------------------------------------------------
# Incremental ordering + float caching. Both are pure performance changes, so
# these pin them to the naive behaviour they replaced.
# ---------------------------------------------------------------------------

def _naive_snapshot(book):
    """Sorts the whole side on every call, as snapshot() did before."""
    return {
        "bids": sorted(({"price": p, "size": s} for p, s in book.bids.items()),
                       key=lambda lv: lv["price"], reverse=True),
        "asks": sorted(({"price": p, "size": s} for p, s in book.asks.items()),
                       key=lambda lv: lv["price"]),
    }


def test_incremental_order_matches_full_resort_after_churn():
    """Inserts, overwrites and deletes in arbitrary price order must leave the
    same ordering a fresh sort would produce."""
    book = Book()
    book.apply_seed([_level("0.40", "10")], [_level("0.60", "20")])
    changes = [
        ("0.35", "7", "BUY"), ("0.45", "3", "BUY"), ("0.38", "1", "BUY"),
        ("0.55", "9", "SELL"), ("0.70", "2", "SELL"), ("0.65", "4", "SELL"),
        ("0.40", "99", "BUY"),    # overwrite an existing level
        ("0.38", "0", "BUY"),     # delete from the middle
        ("0.70", "0", "SELL"),    # delete the far edge
        ("0.36", "6", "BUY"),     # reinsert below a deleted level
    ]
    for price, size, side in changes:
        book.apply_price_change([{"price": Decimal(price), "size": Decimal(size), "side": side}])
        assert book.snapshot() == _naive_snapshot(book)

    assert [lv["price"] for lv in book.snapshot()["bids"]] == [
        Decimal("0.45"), Decimal("0.40"), Decimal("0.36"), Decimal("0.35")]
    assert [lv["price"] for lv in book.snapshot()["asks"]] == [
        Decimal("0.55"), Decimal("0.60"), Decimal("0.65")]


def test_deleting_a_level_that_was_never_present_is_a_no_op():
    book = Book()
    book.apply_seed([_level("0.40", "10")], [_level("0.60", "20")])
    book.apply_price_change([{"price": Decimal("0.11"), "size": Decimal("0"), "side": "BUY"}])
    assert book.snapshot() == _naive_snapshot(book)
    assert len(book.snapshot()["bids"]) == 1


def test_snapshot_floats_matches_snapshot_value_for_value():
    """snapshot_floats() only moves where the float64 conversion happens, so it
    must agree with snapshot() value for value."""
    book = Book()
    book.apply_seed(
        [_level("0.401", "10.5"), _level("0.399", "7")],
        [_level("0.601", "20"), _level("0.605", "0.25")],
    )
    book.apply_price_change([{"price": Decimal("0.402"), "size": Decimal("3"), "side": "BUY"}])
    book.apply_price_change([{"price": Decimal("0.399"), "size": Decimal("0"), "side": "BUY"}])

    exact, as_float = book.snapshot(), book.snapshot_floats()
    for side in ("bids", "asks"):
        assert [lv["price"] for lv in as_float[side]] == [float(lv["price"]) for lv in exact[side]]
        assert [lv["size"] for lv in as_float[side]] == [float(lv["size"]) for lv in exact[side]]
        assert all(isinstance(lv["price"], float) and isinstance(lv["size"], float)
                   for lv in as_float[side])


def test_reseeding_rebuilds_order_and_float_caches():
    book = Book()
    book.apply_seed([_level("0.40", "10")], [_level("0.60", "20")])
    book.apply_price_change([{"price": Decimal("0.30"), "size": Decimal("5"), "side": "BUY"}])
    # A reconnect re-seeds every live token; stale levels must not survive it.
    book.apply_seed([_level("0.50", "1")], [_level("0.70", "2")])
    assert book.snapshot() == _naive_snapshot(book)
    assert book.snapshot_floats() == {
        "bids": [{"price": 0.5, "size": 1.0}],
        "asks": [{"price": 0.7, "size": 2.0}],
    }
