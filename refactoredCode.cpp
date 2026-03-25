#include <unordered_map>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <shared_mutex>
#include <atomic>

/**
 * DESIGN CHOICE: Thread-Safe Price Cache
 * We use a Shared Mutex (RW-Lock) because price data is typically 
 * "Read-Heavy" (many modules want the price) but "Write-Light" 
 * (only the feed handler updates it).
 */
class PriceCache {
    // mutable allows locking within 'const' getter methods
    mutable std::shared_mutex rw_mtx;
    std::unordered_map<std::string, double> prices;

public:
    void update(const std::string& symbol, double px) {
        // unique_lock: Exclusive access for writers
        std::unique_lock lock(rw_mtx);
        prices[symbol] = px;
    }

    double get(const std::string& symbol) const {
        // shared_lock: Multiple readers can enter simultaneously
        std::shared_lock lock(rw_mtx);
        auto it = prices.find(symbol);
        return (it != prices.end()) ? it->second : 0.0;
    }
};

class EventProcessor {
    PriceCache cache;
    std::queue<std::string> queue;
    
    // Sync primitives for the producer-consumer pattern
    std::mutex mtx;
    std::condition_variable cv;
    
    // std::atomic ensures visibility across CPU cores without explicit locks
    std::atomic<bool> running{true};

public:
    /**
     * LOGIC: Thread-Safe Enqueue
     * The 'onEvent' (Producer) must lock the mutex to safely push to 
     * the std::queue, as it is not internally thread-safe.
     */
    void onEvent(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            queue.push(msg);
        }
        // Wake up the worker thread if it is sleeping on cv.wait()
        cv.notify_one(); 
    }

    /**
     * LOGIC: Optimized Consumer Loop
     * Instead of "busy-waiting" (polling), we use a Condition Variable.
     * This puts the thread to sleep, yielding CPU cycles to other processes.
     */
    void worker() {
        while (running) {
            std::string msg;
            {
                std::unique_lock<std::mutex> lock(mtx);
                
                // Wait until the queue has data OR the system is stopping.
                // This prevents "spurious wakeups" from causing errors.
                cv.wait(lock, [this] { return !queue.empty() || !running; });
                
                // Graceful Exit: Process remaining items before dying
                if (!running && queue.empty()) return;

                msg = std::move(queue.front()); // Use move semantics for efficiency
                queue.pop();
            }
            process(msg);
        }
    }

    /**
     * DESIGN CHOICE: Asynchronous Task Offloading
     * In high-frequency systems, 'publish' and 'cache update' are critical path.
     * 'saveToDB' is a blocking I/O operation that could take milliseconds.
     */
    void process(const std::string& msg) {
        auto [symbol, price] = parse(msg);

        // Path A: Low-Latency (Synchronous)
        cache.update(symbol, price);
        publish(symbol, price);

        // Path B: High-Latency (Asynchronous)
        // We offload the DB write so it doesn't "stall" the worker thread.
        // This prevents Head-of-Line blocking.
        std::thread([this, symbol, price]() {
            saveToDB(symbol, price); 
        }).detach(); 
    }

    void stop() {
        running = false;
        cv.notify_all(); // Wake up worker if it's stuck waiting for a message
    }
};
