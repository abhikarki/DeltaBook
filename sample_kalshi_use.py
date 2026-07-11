import os
import time

import cppimport.import_hook
import kalshi_orderbook

os.environ["KALSHI_API_KEY_ID"] = "YOUR_API_KEY"
os.environ["KALSHI_PRIVATE_KEY_PATH"] = "YOUR_PATH_TO_PRIVATE_KEY_FILE"

def main():
    book = kalshi_orderbook.OrderBook("KXBTCMINY-27JAN01-60000.00")

    print("Background thread started to maintain the OrderBook")

    while True:
        snapshot = book.get_best_bid_ask()

        if not snapshot.is_synced:
            time.sleep(0.1)
            continue
            
        print(f"Live Market -> YES Bid: {snapshot.yes_bid} | YES Ask: {snapshot.yes_ask}")
        time.sleep(0.5)

if __name__ == "__main__":
    main()