/* before identification and mordification of code.*/
#include <unordered_map>
#include <string>
#include <queue>
#include <mutex>

class PriceCache {
public:
    std::unordered_map<std::string, double> prices;

    void update(const std::string& symbol, double px) {
        prices[symbol] = px;
    }

    double get(const std::string& symbol) {
        return prices[symbol];
    }
};

class EventProcessor {
    PriceCache cache;
    std::queue<std::string> queue;
    std::mutex mtx;
    bool running = true;

public:
    void onEvent(const std::string& msg) {
        queue.push(msg);  // no lock
    }

    void worker() {
        while (running) {
            if (queue.empty()) continue;

            std::string msg = queue.front();
            queue.pop();

            process(msg);
        }
    }

    void process(const std::string& msg) {
        auto [symbol, price] = parse(msg);

        cache.update(symbol, price);

        publish(symbol, price);

        saveToDB(symbol, price);  // blocking call
    }

    void stop() {
        running = false;
    }
};
