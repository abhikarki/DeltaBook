import os
import time

import cppimport.import_hook
import kalshi_orderbook

os.environ["KALSHI_API_KEY_ID"] = "YOUR_API_KEY"
os.environ["KALSHI_PRIVATE_KEY_PATH"] = "YOUR_PATH_TO_PRIVATE_KEY_FILE"

MARKET_TICKERS = [
    "KXBTCMINY-27JAN01-60000.00",
    "KXBTCMAX100-26-DEC",
]

def main():
    book = kalshi_orderbook.OrderBook(MARKET_TICKERS)

    print("Background thread started to maintain the orderBooks")

    while True:
        for ticker in MARKET_TICKERS:
            snapshot = book.get_best_bid_ask(ticker)

            if not snapshot.is_synced:
                time.sleep(0.1)
                continue
                
            print(f"{ticker} -> YES Bid: {snapshot.yes_bid} | YES Ask: {snapshot.yes_ask}")
            time.sleep(0.5)

if __name__ == "__main__":
    main()