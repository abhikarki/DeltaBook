## DeltaBook

## Performance Benchmarks
| Event Metric | Avg(μs) | Minimum(μs) | Maximum(μs) |
|------------|----------|----------|----------|
| **Network Latency (Kalshi-to-App)** | 40,600 | 32,000 | 89,000 | 
| **Total Message Handling** | 18.6 | 2.57 | 69.2 | 
| **JSON Structural Parsing** | 5.32 | 0.7 | 34.8 | 
| **Message Dispatch** | 11.6 | 0.648 | 53.7 | 
| **Snapshot Parse: YES Outcomes** | 9.96 | 9.96 | 9.96 | 
| **Snapshot Parse: NO Outcomes** | 0.863 | 0.863 | 0.863 |
| **Apply Snapshot to Book** | 2.55 | 2.55 | 2.55 | 
| **Decode Incoming Delta** | 1.5 | 0.222 | 9.19 |
| **Apply Delta to Book** | 0.329 | 0.038 | 1.29 |
<br>
The data processing for our program begins at the network layer with socket read, which blocks execution wile waiting for packets. The big difference in its average versus minimum times occurs due to the blocking behavior: when the market is quiet, this metric include idle waiting time, whereas back to back packets process instantly out of the socket resulting in the minimum time for socket read of 0.146 μs. After the socket unblocks, Network Latency is calculated by the absolute time difference between Kalshi's server timestamp and current time. This metric measures the elapsed time from the moment Kalshi publishes the message until it reaches our program (also including the time through our NIC, kernel, and OpenSSL decryption to user space.) <br>
Once the data reaches our program, the engine initiates Total Message Handling Lifecycle to record total time for handling the data. The data goes through JSON Structural Parsing before passing to Message Type Dispatching (handling the logic for initial snapshot or delta type). If it is initial snapshot data, we record time for parsing (YES and NO) and applying snapshot to book, and if it is orderbook delta, we record time to decode delta and applying delta to book.

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
