import os
import time

import cppimport.import_hook


import kalshi_orderbook

os.environ["KALSHI_API_KEY_ID"] = "YOUR_API_KEY"
os.environ["KALSHI_PRIVATE_KEY_PATH"] = "YOUR_PATH_TO_PRIVATE_KEY_FILE"

MARKET_TICKERS = [
    "KXPGATOUR-3MO26-SMAZ",
    "KXPGATOUR-3MO26-TCAM",
]

POLL_INTERVAL_SECONDS = 2.0


def format_levels(levels, title):
    lines = [f"  {title}"]
    if not levels:
        lines.append("    (no levels yet)")
        return "\n".join(lines)

    lines.append("    price     | volume")
    lines.append("    -----------------")
    for level in levels:
        price = level.price_ticks / 10000
        size = level.size / 100
        lines.append(f"    {price:>8.4f} | {size:>7.2f}")
    return "\n".join(lines)


def print_book(ticker, yes_levels, no_levels):
    print(f"\n{ticker}")
    print(format_levels(list(yes_levels), "YES levels"))
    print(format_levels(list(no_levels), "NO levels"))

def main():
    book = kalshi_orderbook.OrderBook(MARKET_TICKERS)

    print("Background thread started to maintain the orderBooks")

    while True:
        for ticker in MARKET_TICKERS:
            full_levels = book.get_full_levels(ticker)
            print_book(ticker, full_levels.yes_levels, full_levels.no_levels)

            # snapshot = book.get_best_bid_ask(ticker)
            # if not snapshot.is_synced:
            #     continue
            # print(f"{ticker} -> YES Bid: {snapshot.yes_bid} | YES Ask: {snapshot.yes_ask}")

        time.sleep(POLL_INTERVAL_SECONDS)

if __name__ == "__main__":
    main()