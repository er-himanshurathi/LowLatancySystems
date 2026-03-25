#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <future>

/**
 * DESIGN CHOICE: Reader-Writer Lock (std::shared_mutex)
 * USE CASE: High-read, low-write scenarios.
 * If multiple services need to check prices simultaneously, they shouldn't block each other.
 * Only an 'update' (write) requires exclusive access.
 */
class PriceCache {
    mutable std::shared_mutex rw_mtx; 
    std::unordered_map<std::string, double> prices;

public:
    void update(const std::string& symbol, double px) {
        std::unique_lock lock(rw_mtx); // Exclusive lock for writing
        prices[symbol] = px;
    }

    double get(const std::string& symbol) const {
        std::shared_lock lock(rw_mtx); // Shared lock for reading
        auto it = prices.find(symbol);
        return (it != prices.end()) ? it->second : -1.0; 
    }
};

class EventProcessor {
private:
    PriceCache cache;
    
    // --- Synchronization Primitives ---
    std::queue<std::string> queue;
    std::mutex queue_mtx;
    std::condition_variable cv;
    
    /** * DESIGN CHOICE: std::atomic<bool>
     * USE CASE: Thread signaling.
     * Guarantees that when stop() is called, the worker thread immediately sees 
     * the change across CPU cache lines without needing a heavy mutex lock.
     */
    std::atomic<bool> running{true};
    std::thread worker_thread;

    // Simulation of external dependencies
    std::pair<std::string, double> parse(const std::string& msg) { return {"AAPL", 150.0}; }
    void publish(const std::string& s, double p) { /* High-speed broadcast */ }
    void saveToDB(const std::string& s, double p) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }

public:
    EventProcessor() {
        // Start the consumer thread immediately upon object creation
        worker_thread = std::thread(&EventProcessor::worker, this);
    }

    /**
     * DESIGN CHOICE: RAII Destructor
     * USE CASE: Production reliability.
     * Ensures that if the object is deleted, the threads are shut down gracefully
     * rather than leaving a "zombie" thread running in the background.
     */
    ~EventProcessor() {
        stop();
    }

    /**
     * USE CASE: The "Producer" (likely a network socket thread).
     * We use std::move to transfer ownership of the string to the queue.
     * This avoids a deep copy of the string data, reducing latency.
     */
    void onEvent(std::string msg) {
        {
            std::lock_guard<std::mutex> lock(queue_mtx);
            queue.push(std::move(msg));
        }
        cv.notify_one(); // Wake up the worker thread
    }

    /**
     * DESIGN CHOICE: Condition Variable Wait
     * USE CASE: Power efficiency and CPU availability.
     * Instead of "spinning" (using 100% CPU checking the queue), the thread 
     * sleeps and is woken up by the OS only when there is actual work to do.
     */
    void worker() {
        while (true) {
            std::string msg;
            {
                std::unique_lock<std::mutex> lock(queue_mtx);
                // Wait until there's a message OR we are shutting down
                cv.wait(lock, [this] { return !queue.empty() || !running; });

                // DRAIN THE QUEUE: Ensure we process remaining items before exiting
                if (!running && queue.empty()) break;

                msg = std::move(queue.front());
                queue.pop();
            }

            try {
                process(msg);
            } catch (...) {
                // Prevent an exception in one message from killing the whole system
            }
        }
    }

    /**
     * DESIGN CHOICE: Asynchronous Persistence
     * USE CASE: Latency Hiding.
     * The 'Hot Path' (Cache + Publish) is finished in microseconds.
     * The 'Slow Path' (DB) is offloaded to a background task so it doesn't 
     * delay the processing of the next price update in the queue.
     */
    void process(const std::string& msg) {
        auto [symbol, price] = parse(msg);

        // Update internal state and notify other fast systems
        cache.update(symbol, price);
        publish(symbol, price);

        // Offload the slow blocking I/O to a separate thread
        std::async(std::launch::async, [this, symbol, price]() {
            saveToDB(symbol, price);
        });
    }

    void stop() {
        running = false;
        cv.notify_all(); // Wake up worker if it's sleeping so it can exit
        if (worker_thread.joinable()) {
            worker_thread.join();
        }
    }
};
