## DeltaBook

## Performance Benchmarks
| Event Metric | Avg(μs) | Minimum(μs) | Maximum(μs) |
|------------|----------|----------|----------|
| **Network Latency (Wire-to-App)** | 40,600 | 32,000 | 89,000 | 
| **WebSocket Socket Read** |  292,000 | 0.146 | 7,470,000 | 
| **Total Message Handling** | 18.6 | 2.57 | 69.2 | 
| **JSON Structural Parsing** | 5.32 | 0.7 | 34.8 | 
| **Message Dispatch** | 11.6 | 0.648 | 53.7 | 
| **Snapshot Parse: YES Outcomes** | 9.96 | 9.96 | 9.96 | 
| **Snapshot Parse: NO Outcomes** | 0.863 | 0.863 | 0.863 |
| **Apply Snapshot to Book** | 2.55 | 2.55 | 2.55 | 
| **Decode Incoming Delta** | 1.5 | 0.222 | 9.19 |
| **Apply Delta to Book** | 0.329 | 0.038 | 1.29 |

<br>

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
