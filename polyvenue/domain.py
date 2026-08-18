"""Price and size arithmetic: mils, the tick grid, complement, lots.

Prices are integer mils (0.93 == 930) so grid and equality checks stay exact.
No function raises on a price; an untradeable price is a fact the quoter reports.
Tick size and minimum order size do raise, since a zero there breaks every
division that follows.

Rounding depends on direction. Prices from the venue round to nearest, because
off-grid arrivals are normal. Prices we send snap by side: bids down, asks up.
"""
from decimal import Decimal, ROUND_HALF_UP

MILS_PER_UNIT = 1000          # 0.93 -> 930. Finest tick seen live is 0.001 == 1 mil.

# -- representation ---------------------------------------------------------
def to_mils(price: Decimal) -> int:
    """Price -> mils, rounded to nearest. Never asserts: arriving prices are often off-grid."""
    price_in_mils = int((price * MILS_PER_UNIT).quantize(Decimal('1'), rounding=ROUND_HALF_UP))
    return price_in_mils

def to_decimal(mils: int) -> Decimal:
    """Mils -> exact price, for handing to the gateway."""
    return Decimal(mils) / MILS_PER_UNIT

def tick_to_mils(tick_size: Decimal) -> int:
    """0.01 -> 10, 0.001 -> 1. Raises below one mil: a 0-mil tick divides by zero downstream."""
    tick_mils = to_mils(tick_size)
    if tick_mils <= 0:
        raise ValueError(
            f"tick size {tick_size} is finer than one mil ({Decimal(1) / MILS_PER_UNIT}); "
            "prices can no longer be represented exactly as mils"
        )
    return tick_mils

# -- the grid ---------------------------------------------------------------
def floor_to_grid(mils: int, tick_mils: int) -> int:
    """Largest on-grid price <= mils. What a BID takes: never pay more."""
    return (mils // tick_mils) * tick_mils

def ceil_to_grid(mils: int, tick_mils: int) -> int:
    """Smallest on-grid price >= mils. What an ASK takes: never sell cheaper."""
    # Via floor division so the two stay exact inverses; `//` floors toward negative
    # infinity, so negating both sides keeps this right below zero too.
    return -((-mils) // tick_mils) * tick_mils

def is_on_grid(mils: int, tick_mils: int) -> bool:
    """True if mils sits exactly on the tick grid."""
    return mils % tick_mils == 0

# -- the two sides ----------------------------------------------------------
def complement(mils: int) -> int:
    """930 -> 70. UP + DOWN == 1, so this turns a SELL into a BUY.

    Needed because the venue rejects a naked short (verified: 400, balance 0) -- "sell UP
    at 0.93" is placed as "buy DOWN at 0.07". Note it INVERTS rounding:
    complement(ceil_to_grid(p)) == floor_to_grid(complement(p)), which holds only while the
    tick divides MILS_PER_UNIT. Both live regimes do; one that did not would cross quotes
    silently, so a test pins it.
    """
    return MILS_PER_UNIT - mils

# -- what's tradeable -------------------------------------------------------
def is_tradeable(mils: int) -> bool:
    """Strictly inside (0, 1) -- 0 and 1 are settled outcomes, not prices.

    Binds in the wings, which is where the flow is (R6): a 0.985 mid plus a 30-mil
    half-spread wants an ask at 1.015. The quoter asks this and reports NO_VALID_QUOTE.
    """
    return 0 < mils < MILS_PER_UNIT

# -- size -------------------------------------------------------------------
def lots_to_shares(lots: int, min_order_size: Decimal) -> Decimal:
    """Lots -> shares. A lot is the minimum order: 5 shares on every 5m and 15m market."""
    if min_order_size <= 0:
        raise ValueError(f"minimum order size {min_order_size} must be positive")
    return min_order_size * lots

def shares_to_lots(shares: Decimal, min_order_size: Decimal) -> int:
    """Whole lots in `shares`, rounded DOWN -- the exact inverse of lots_to_shares.

    So it understates a partial lot (7 shares -> 1), and partial lots are real: one fill
    against a 1-lot cap overshoots to 5 shares. A risk check that must not understate
    rounds up at the call site rather than have this decide silently (standing rule 4).
    """
    if min_order_size <= 0:
        raise ValueError(f"minimum order size {min_order_size} must be positive")
    return int(shares // min_order_size)
