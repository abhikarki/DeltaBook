## DeltaBook

## setup and compile
```bash
export KALSHI_API_KEY_ID="YOUR_API_KEY_ID"
export KALSHI_PRIVATE_KEY_PATH="YOUR_PRIVATE_KEY_FILE_PATH"
```

```bash
make clean
make main_client
```

## Run
use the market ticker of your interest
```bash
./main_client {market_ticker}
```

## Test
bulld test runner using doctest and run tests:
```bash
make run_tests
./run_tests
```
## Python use case
Look at sample_kalshi_use.py
