## DeltaBook

## Performance Benchmarks
| Event Metric | Avg(μs) | Minimum(μs) | Maximum(μs) |
|------------|----------|----------|----------|
| **JSON Parsing** | 2.81 | 0.475 | 35.1 | 
| **Apply Parsed Baseline Snapshot to Book** | 3.43 | 0.418 | 6.43 | 
| **Apply Parsed Delta to Book** | 0.307 | 0.048 | 3.72 |

<br>
Here, each Kalshi websocket message is received as JSON. “JSON parsing” measures the time spent decoding one websocket message and extracting the relevant fields into a structured 'ParsedUpdate'. “Apply Parsed Baseline Snapshot to Book” measures the time spent applying the snapshot, which may contain many price levels, into the local orderbook. “Apply Parsed Delta to Book” measures the time spent applying the delta (Parsed into ParsedUpdate) to the local orderbook state.

## Low-Overhead Telemetry
<img width="650" height="400" alt="DeltaBook_Telemetry_v2" src="https://github.com/user-attachments/assets/f2f81f84-f03d-48fb-a7f7-38d01242c6a8" /> <br>
Adding benchmarking logic and timestamps directly inside business logic (for example, modifying the orderbook delta structures to also carry timestamps and labels to see timing at different points) can bloat their size and pollute CPU core's L1 cache which slows down the memory writes on the hot path. To eliminate this overhead, we decouple exectuion from monitoring. Core 1 and Core 2 (isolated cores for running the main orderbook logic) capture raw time measurements instantly using the hardware CPU cycle counter (__rdtsc()) in just a few nanoseconds, then immediately offload the data into pre allocated SPSC telemetry queues. For the queues, we use local integer copy (cached_read_index) for checking boundaries instead of querying other cores repeatedly. Thread running on Core 3 is responsible for polling the telemetry queues in the background, so our core orderbook logic and data path is not affected by logic for capturing metrics.



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
./main_client {market_tickers_separated_by space}
```

## Test
bulld test runner using doctest and run tests:
```bash
make run_tests
./run_tests
```
## Python use case
Look at sample_kalshi_use.py
