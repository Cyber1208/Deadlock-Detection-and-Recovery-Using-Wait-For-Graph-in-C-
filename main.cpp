#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <random>

using namespace std;

// Constants
const int NUM_RESOURCES = 3;
const int NUM_THREADS = 7; // Increased number of threads, last is rogue

// Shared state
vector<int> available = { 3, 2, 2 };
vector<vector<int>> maxDemand = {
    {2, 1, 1},
    {1, 1, 1},
    {1, 2, 1},
    {1, 1, 2},
    {2, 1, 1},
    {1, 2, 2},
    {2, 1, 2},
    {3, 2, 2} // Rogue thread
};

vector<vector<int>> allocation(NUM_THREADS + 1, vector<int>(NUM_RESOURCES, 0));
vector<vector<int>> need(NUM_THREADS + 1, vector<int>(NUM_RESOURCES, 0));

vector<vector<int>> request(NUM_THREADS + 1, vector<int>(NUM_RESOURCES, 0)); // requested resources waiting for allocation
vector<bool> threadAlive(NUM_THREADS + 1, true);
vector<bool> isHolding(NUM_THREADS + 1, false);

mutex mtx;
atomic<bool> running(false);
atomic<bool> simulationActive(false);
atomic<bool> deadlockDetected(false);
int killedThread = -1;

// Helper random
random_device rd;
mt19937 gen(rd());

// Requests a random resource unit for a thread (if available)
bool tryRequestResources(int tid) {
    lock_guard<mutex> lock(mtx);
    if (!threadAlive[tid]) return false;

    uniform_int_distribution<> dist(0, NUM_RESOURCES - 1);
    int r = dist(gen);

    // Request 1 unit of resource r if available and need permits
    if (need[tid][r] > 0 && available[r] > 0) {
        request[tid][r] = 1; // mark as requested (waiting temporarily)
        // For demo, grant immediately if available
        available[r]--;
        allocation[tid][r]++;
        need[tid][r]--;
        request[tid][r] = 0; // granted
        isHolding[tid] = true;
        return true;
    }
    else {
        // Waiting for resource r
        request[tid][r] = 1;
        isHolding[tid] = false;
        return false;
    }
}

void releaseResources(int tid) {
    lock_guard<mutex> lock(mtx);
    for (int i = 0; i < NUM_RESOURCES; i++) {
        available[i] += allocation[tid][i];
        need[tid][i] += allocation[tid][i];
        allocation[tid][i] = 0;
        request[tid][i] = 0;
    }
    isHolding[tid] = false;
}

// Worker thread logic
void workerThread(int tid) {
    while (simulationActive) {
        if (!running || !threadAlive[tid]) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        if (tryRequestResources(tid)) {
            this_thread::sleep_for(chrono::milliseconds(700));
            releaseResources(tid);
            this_thread::sleep_for(chrono::milliseconds(300));
        }
        else {
            this_thread::sleep_for(chrono::milliseconds(200));
        }
    }
}

// Rogue thread tries to request all resources
void rogueThread() {
    int tid = NUM_THREADS;
    while (simulationActive) {
        if (!running || !threadAlive[tid]) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        unique_lock<mutex> lock(mtx);
        bool canRequestAll = true;
        for (int r = 0; r < NUM_RESOURCES; r++) {
            if (need[tid][r] <= 0 || available[r] <= 0) {
                canRequestAll = false;
                break;
            }
        }
        if (canRequestAll) {
            for (int r = 0; r < NUM_RESOURCES; r++) {
                available[r]--;
                allocation[tid][r]++;
                need[tid][r]--;
                request[tid][r] = 0;
            }
            isHolding[tid] = true;
            lock.unlock();
            this_thread::sleep_for(chrono::milliseconds(1000));
            releaseResources(tid);
            this_thread::sleep_for(chrono::milliseconds(300));
        }
        else {
            for (int r = 0; r < NUM_RESOURCES; r++) request[tid][r] = 1;
            isHolding[tid] = false;
            lock.unlock();
            this_thread::sleep_for(chrono::milliseconds(500));
        }
    }
}

// Deadlock detection with detailed logging
void deadlockDetector() {
    while (simulationActive) {
        if (!running) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        unique_lock<mutex> lock(mtx);

        int totalRequesting = 0;
        int totalAvailable = 0;
        for (int r = 0; r < NUM_RESOURCES; r++) {
            totalAvailable += available[r];
            for (int t = 0; t <= NUM_THREADS; t++) {
                if (request[t][r] == 1) totalRequesting++;
            }
        }

        // Logging current requests
        cout << "Deadlock Detector: Total Available Resources: " << totalAvailable
            << ", Total Requests: " << totalRequesting << endl;

        cout << "Waiting threads: ";
        for (int t = 0; t <= NUM_THREADS; t++) {
            bool waiting = false;
            for (int r = 0; r < NUM_RESOURCES; r++) {
                if (request[t][r] == 1) {
                    waiting = true;
                    break;
                }
            }
            if (waiting) cout << t << " ";
        }
        cout << endl;

        if (totalRequesting > totalAvailable) {
            deadlockDetected = true;

            int victim = -1;
            for (int t = 0; t <= NUM_THREADS; t++) {
                if (threadAlive[t]) {
                    bool waiting = false;
                    for (int r = 0; r < NUM_RESOURCES; r++) {
                        if (request[t][r] == 1) {
                            waiting = true;
                            break;
                        }
                    }
                    if (waiting) {
                        victim = t;
                        break;
                    }
                }
            }

            if (victim != -1) {
                cout << "Deadlock detected! Killing thread " << victim << endl;
                threadAlive[victim] = false;
                for (int r = 0; r < NUM_RESOURCES; r++) {
                    available[r] += allocation[victim][r];
                    allocation[victim][r] = 0;
                    need[victim][r] = maxDemand[victim][r];
                    request[victim][r] = 0;
                }
                isHolding[victim] = false;
                killedThread = victim;
            }
            else {
                cout << "Deadlock detected but no suitable victim found!" << endl;
            }
        }
        else {
            deadlockDetected = false;
            killedThread = -1;
        }

        lock.unlock();
        this_thread::sleep_for(chrono::milliseconds(500));
    }
}

int main() {
    sf::RenderWindow window(sf::VideoMode(1200, 700), "Deadlock Visual Simulation");

    sf::Font font;
    if (!font.loadFromFile("arial.ttf")) {
        cout << "Failed to load font" << endl;
        return -1;
    }

    sf::RectangleShape startBtn({ 100, 40 });
    startBtn.setFillColor(sf::Color::Green);
    startBtn.setPosition(20, 650);
    sf::Text startTxt("Start", font, 20);
    startTxt.setPosition(40, 655);

    sf::RectangleShape stopBtn({ 100, 40 });
    stopBtn.setFillColor(sf::Color::Red);
    stopBtn.setPosition(140, 650);
    sf::Text stopTxt("Stop", font, 20);
    stopTxt.setPosition(160, 655);

    // Positions for threads and resources (resources vertically aligned)
    vector<sf::Vector2f> threadPos;
    for (int i = 0; i <= NUM_THREADS; i++) {
        threadPos.push_back(sf::Vector2f(100, 50 + i * 70));
    }

    vector<sf::Vector2f> resourcePos = {
        {600, 100}, {600, 200}, {600, 300}
    };

    // Reset data
    {
        lock_guard<mutex> lock(mtx);
        available = { 3, 2, 2 };
        allocation.assign(NUM_THREADS + 1, vector<int>(NUM_RESOURCES, 0));
        need = maxDemand;
        request.assign(NUM_THREADS + 1, vector<int>(NUM_RESOURCES, 0));
        threadAlive.assign(NUM_THREADS + 1, true);
        isHolding.assign(NUM_THREADS + 1, false);
        deadlockDetected = false;
        killedThread = -1;
    }

    vector<thread> workers;
    thread rogue;
    thread detector;

    simulationActive = false;
    running = false;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                simulationActive = false;
                running = false;
                window.close();
            }
            if (event.type == sf::Event::MouseButtonPressed) {
                sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                if (startBtn.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                    if (!simulationActive) {
                        simulationActive = true;
                        running = true;

                        // Reset data
                        {
                            lock_guard<mutex> lock(mtx);
                            available = { 3, 2, 2 };
                            allocation.assign(NUM_THREADS + 1, vector<int>(NUM_RESOURCES, 0));
                            need = maxDemand;
                            request.assign(NUM_THREADS + 1, vector<int>(NUM_RESOURCES, 0));
                            threadAlive.assign(NUM_THREADS + 1, true);
                            isHolding.assign(NUM_THREADS + 1, false);
                            deadlockDetected = false;
                            killedThread = -1;
                        }

                        // Launch worker threads
                        for (int i = 0; i < NUM_THREADS; i++) {
                            workers.emplace_back(workerThread, i);
                        }
                        // Rogue thread
                        rogue = thread(rogueThread);

                        // Deadlock detector
                        detector = thread(deadlockDetector);

                        cout << "Simulation started" << endl;
                    }
                    else {
                        running = true;
                        cout << "Simulation resumed" << endl;
                    }
                }
                if (stopBtn.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                    running = false;
                    cout << "Simulation paused" << endl;
                }
            }
        }

        window.clear(sf::Color::Black);

        // Draw threads
        for (int i = 0; i <= NUM_THREADS; i++) {
            sf::CircleShape circ(20);
            circ.setPosition(threadPos[i]);
            circ.setFillColor(threadAlive[i] ? (isHolding[i] ? sf::Color::Green : sf::Color::Yellow) : sf::Color(100, 100, 100));
            window.draw(circ);

            sf::Text txt("T" + to_string(i), font, 15);
            txt.setPosition(threadPos[i].x + 10, threadPos[i].y + 10);
            window.draw(txt);
        }

        // Draw resources vertically
        for (int i = 0; i < NUM_RESOURCES; i++) {
            sf::RectangleShape rect({ 60, 30 });
            rect.setPosition(resourcePos[i]);
            rect.setFillColor(sf::Color::Cyan);
            window.draw(rect);

            sf::Text txt("R" + to_string(i) + ": " + to_string(available[i]), font, 15);
            txt.setPosition(resourcePos[i].x + 5, resourcePos[i].y + 5);
            window.draw(txt);
        }

        // Draw arrows for allocation: Thread -> Resource
        for (int t = 0; t <= NUM_THREADS; t++) {
            for (int r = 0; r < NUM_RESOURCES; r++) {
                if (allocation[t][r] > 0) {
                    sf::Vertex line[] =
                    {
                        sf::Vertex(sf::Vector2f(threadPos[t].x + 20, threadPos[t].y + 20), sf::Color::White),
                        sf::Vertex(sf::Vector2f(resourcePos[r].x, resourcePos[r].y + 15), sf::Color::White)
                    };
                    window.draw(line, 2, sf::Lines);

                    // Draw arrowhead
                    sf::CircleShape arrowHead(5, 3);
                    arrowHead.setFillColor(sf::Color::White);
                    arrowHead.setPosition(resourcePos[r].x - 7, resourcePos[r].y + 10);
                    arrowHead.setRotation(90);
                    window.draw(arrowHead);
                }
            }
        }

        // Draw arrows for request: Thread -> Resource (dashed or red)
        for (int t = 0; t <= NUM_THREADS; t++) {
            for (int r = 0; r < NUM_RESOURCES; r++) {
                if (request[t][r] == 1) {
                    sf::Vertex line[] =
                    {
                        sf::Vertex(sf::Vector2f(threadPos[t].x + 20, threadPos[t].y + 20), sf::Color::Red),
                        sf::Vertex(sf::Vector2f(resourcePos[r].x, resourcePos[r].y + 15), sf::Color::Red)
                    };
                    window.draw(line, 2, sf::Lines);

                    // Arrowhead
                    sf::CircleShape arrowHead(5, 3);
                    arrowHead.setFillColor(sf::Color::Red);
                    arrowHead.setPosition(resourcePos[r].x - 7, resourcePos[r].y + 10);
                    arrowHead.setRotation(90);
                    window.draw(arrowHead);
                }
            }
        }

        // Draw buttons
        window.draw(startBtn);
        window.draw(startTxt);
        window.draw(stopBtn);
        window.draw(stopTxt);

        // Show deadlock status on screen
        sf::Text deadlockTxt("", font, 20);
        deadlockTxt.setPosition(800, 650);
        if (deadlockDetected) {
            deadlockTxt.setString("Deadlock detected! Killed T" + to_string(killedThread));
            deadlockTxt.setFillColor(sf::Color::Red);
        }
        else {
            deadlockTxt.setString("No deadlock");
            deadlockTxt.setFillColor(sf::Color::Green);
        }
        window.draw(deadlockTxt);

        window.display();
    }

    // Cleanup threads on exit
    simulationActive = false;
    running = false;
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    if (rogue.joinable()) rogue.join();
    if (detector.joinable()) detector.join();

    return 0;
}
