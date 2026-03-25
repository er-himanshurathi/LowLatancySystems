# LowLatancySystems

Part 3 (Syatem Design)#######################
Question 1: /**************/
Ensuring strict message ordering in Kafka while maintaining high throughput is a classic engineering challenge. When dealing with FX (Foreign Exchange) symbols, the goal is usually to ensure that all updates for EUR/USD are processed in order, even if they arrive at the same time as updates for GBP/JPY.
The secret sauce lies in how you handle partitioning and consumer-side threading.
1. Semantic Partitioning (The Foundation)
Kafka only guarantees ordering within a single partition. To ensure all messages for a specific FX symbol (e.g., USD/JPY) land in the same partition, you must use a Consistent Hashing Partition strategy.
The Producer Key: Set the Kafka message key to the FX_Symbol.
The Result: Kafka’s default partitioner hashes this key, ensuring that every message for USD/JPY always goes to Partition 4, while EUR/GBP always goes to Partition 9.
2. The "One Consumer, One Partition" Rule
By default, a single consumer in a consumer group will pull messages from one or more partitions. If you process messages sequentially in a single thread, ordering is guaranteed.
The Trade-off: This limits your concurrency. If one symbol has a massive spike in volume (e.g., during a central bank announcement), that consumer might lag, delaying updates for all other symbols assigned to that same partition.
3. Scaling with Parallel Processors (The "Sharding" Pattern)
If you need to process symbols in parallel but keep them ordered, you can use an In-Memory Sharded Queue within your consumer application.
The Dispatcher: A single-threaded Kafka consumer pulls a batch of messages.
The Worker Pool: You maintain a pool of internal worker threads.
The Routing: The Dispatcher hashes the FX_Symbol again to route the message to a specific internal queue (e.g., Thread 1 handles A-G, Thread 2 handles H-M).
Crucial Note: You must only commit the Kafka offset after all messages in a batch have been successfully processed by the worker threads to avoid data loss.
4. Handling Consumer Rebalancing
When a new consumer joins the group and partitions are reassigned, you risk "race conditions" where two different consumers briefly think they own the same FX symbol.
Solution: Use Consumer Rebalance Listeners. When a partition is revoked, the consumer must "drain" its internal buffers and finish processing current messages before allowing the new consumer to take over.

Question 2: /**************/
**What can go wrong:**

* Partial failures → DB and cache out of sync
* Race conditions → stale data overwrites fresh data
* Cache stampede → DB overload
* Replication lag → stale reads get re-cached
* Network issues → temporary inconsistency

**How to handle consistency:**

* Cache-aside (most common):** write to DB, then invalidate cache
* Add **TTL** to auto-expire stale data
* Use **double delete** to reduce race conditions
* Use **locks or versioning** for concurrent updates
* Use **event-driven updates** for large systems

**Key idea:**
Treat DB as the source of truth and accept **eventual consistency** with safeguards.

Question 3: /**************/

Here’s a concise approach:

* Define the issue:** Identify which endpoints, regions, or users are affected and when it started.
* Check recent changes:** Look for deployments, config updates, or traffic spikes.
* Trace the request path:** Use tools like Jaeger to see where time is spent.
* Inspect infrastructure:** Review CPU, memory, and network metrics with Prometheus / Grafana.
* Analyze database & dependencies:** Look for slow queries, locks, or external service delays.
* Check application issues:** Thread exhaustion, blocking calls, or GC pauses.
* Validate & fix:** Form a hypothesis, confirm it with data, apply a fix, and verify improvement.

Part 4 (AI useage) #######################
1. What the AI was used for
The primary goal was to transform a "sketch" of a system into a thread-safe, production-ready architecture.
This involved:
Identifying Race Conditions: Spotting that std::unordered_map and std::queue are not thread-safe and would cause memory corruption under load.
Architectural Pattern Selection: Implementing the Producer-Consumer pattern to decouple the high-frequency data arrival from the slower processing and storage logic.
Latency Profiling: Recognizing that the saveToDB call was a "blocking" operation that would stall the entire system, and recommending asynchronous offloading.

2. Suggestions Accepted vs. Rejected
Accepted Suggestions:
Condition Variables: I implemented std::condition_variable instead of a simple loop. This allows the CPU to sleep when no data is present, rather than "spinning" at 100% usage.
Shared Mutex (RW-Lock): I chose std::shared_mutex for the price cache. This allows multiple threads to read prices simultaneously while only blocking when a new price update (a write) arrives.
Atomic Flags: I used std::atomic<bool> for the shutdown signal to ensure that the change is immediately visible across all CPU cores without needing a heavy mutex lock.
Rejected/Modified Suggestions:
Lock-Free Queues: While faster, I "rejected" adding a lock-free queue in this version because they are significantly harder to maintain and debug. For most systems, a standard mutex-protected queue is the right balance of performance and safety.
Raw std::thread::detach: In a high-traffic production system, spawning a new thread for every single database write (as shown in the simplified code) would eventually crash the OS. A Thread Pool is the better "next step" to limit the number of concurrent DB connections.

Validation of Correctness, Performance, and Safety
To move from this AI-generated code to a live trading or tracking environment, a developer should perform the following three-step validation:
Thread Safety Validation (Correctness)
The most important tool here is ThreadSanitizer (TSan). You would compile the code with special flags and run a heavy simulation. TSan monitors every memory access and will throw an immediate error if two threads access the same variable without a proper lock. This catches the "silent" bugs that only happen once every million executions.
Latency and Throughput Validation (Performance)
Using a tool like Google Benchmark, you would measure the "tick-to-trade" latency. You need to ensure that updating the PriceCache takes microseconds, not milliseconds. If the shared_mutex becomes a bottleneck due to too many readers, you might then move to a "Lock-Free" or "RCU" (Read-Copy-Update) data structure.
Robustness Under Pressure (Safety)
To ensure safety, you would perform "Burst Testing." This involves flooding the onEvent function with a massive spike of data (e.g., 100,000 events in one second) to see how the queue grows. This validates whether the system requires Backpressure—a mechanism to tell the data source to slow down if the internal queue gets too large and threatens to run the system out of memory.


AI tools used: chatgpt, gemini and claude.

