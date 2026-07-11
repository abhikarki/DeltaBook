## DeltaBook

## Telemetry
<img width="650" height="400" alt="DeltaBook_Telemetry_v2" src="https://github.com/user-attachments/assets/f2f81f84-f03d-48fb-a7f7-38d01242c6a8" />



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
