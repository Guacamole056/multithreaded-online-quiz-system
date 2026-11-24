// quiz_core.hpp
// Concurrency core of the Multi-threaded Online Quiz System.
// Header-only, no GUI dependency, so it can be stress-tested headlessly.
//
//   QuestionBank     std::shared_mutex       many concurrent readers, rare writers
//   Scoreboard       std::mutex              every score/progress update is atomic
//   SubmissionQueue  std::counting_semaphore limits concurrent "database" writes (3)
//   Submission log   std::mutex              no interleaved log lines
//   Statistics       std::atomic             lock-free counters / peak tracking
//   Student threads  one per student
//   Admin threads    one per admin, owns a group of student threads
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <semaphore>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace quiz {

using Clock = std::chrono::steady_clock;

inline std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- Questions
struct Question {
    std::string text;
    std::vector<std::string> options;
    int correct = 0;  // index into options
    int id = 0;       // assigned by the bank
};

inline std::vector<Question> defaultQuestions() {
    return {
        {"Which primitive guarantees only one thread at a time enters a critical section?",
         {"Mutex", "Barrier", "Condition flag", "Pipe"}, 0},
        {"What does a counting semaphore with initial value 3 allow?",
         {"Exactly one holder", "Up to three concurrent holders", "Only three acquisitions ever", "Unlimited holders"}, 1},
        {"A race condition occurs when...",
         {"Two threads run on different cores", "The result depends on unsynchronized thread interleaving", "A thread sleeps too long", "A mutex is destroyed"}, 1},
        {"Which is NOT a necessary condition for deadlock?",
         {"Mutual exclusion", "Hold and wait", "Preemption of resources", "Circular wait"}, 2},
        {"Which C++ lock type lets many readers share a std::shared_mutex?",
         {"std::lock_guard", "std::shared_lock", "std::unique_lock", "std::scoped_lock"}, 1},
        {"Time complexity of binary search on a sorted array of n elements?",
         {"O(n)", "O(log n)", "O(n log n)", "O(1)"}, 1},
        {"Which data structure follows FIFO order?",
         {"Stack", "Queue", "Heap", "Tree"}, 1},
        {"What does RAII stand for in C++?",
         {"Resource Acquisition Is Initialization", "Random Access In Iteration", "Reference And Inheritance Interface", "Runtime Allocation Is Immediate"}, 0},
        {"Which scheduling algorithm can starve long processes?",
         {"Round Robin", "Shortest Job First", "FCFS", "Multilevel feedback with aging"}, 1},
        {"How do threads of one process relate to its memory?",
         {"Fully separate address spaces", "They share the address space and heap", "They share nothing", "They share only registers"}, 1},
        {"Which HTTP status code means 'Not Found'?",
         {"200", "301", "404", "500"}, 2},
        {"Which OSI layer does TCP operate on?",
         {"Network", "Transport", "Session", "Data link"}, 1},
        {"In ACID, what does the 'I' stand for?",
         {"Integrity", "Isolation", "Indexing", "Idempotence"}, 1},
        {"Which SQL clause filters grouped rows?",
         {"WHERE", "HAVING", "ORDER BY", "LIMIT"}, 1},
        {"What is std::atomic<int> used for?",
         {"Lock-free thread-safe operations on an integer", "Faster integer division", "Automatic memory freeing", "Compile-time constants"}, 0},
        {"Which page replacement algorithm suffers from Belady's anomaly?",
         {"LRU", "Optimal", "FIFO", "Clock"}, 2},
        {"What is thrashing?",
         {"Excessive paging that cripples performance", "CPU overheating", "A disk failure", "A stack overflow"}, 0},
        {"How many bits are in a byte?",
         {"4", "8", "16", "32"}, 1},
        {"Which C++ keyword prevents modification of a variable?",
         {"static", "const", "volatile", "mutable"}, 1},
        {"Which BST traversal yields sorted order?",
         {"Preorder", "Inorder", "Postorder", "Level-order"}, 1},
        {"What does a condition variable let a thread do?",
         {"Wait until notified that a condition may hold", "Allocate memory", "Kill another thread", "Share a stack"}, 0},
        {"Priority inversion occurs when...",
         {"A high-priority task waits on a lock held by a low-priority task", "Two tasks have equal priority", "The compiler reverses priorities", "An interrupt preempts a task"}, 0},
        {"Average-case complexity of quicksort?",
         {"O(n)", "O(n log n)", "O(n^2)", "O(log n)"}, 1},
        {"What does DNS do?",
         {"Translates domain names to IP addresses", "Encrypts web traffic", "Routes packets", "Assigns MAC addresses"}, 0},
        {"Which protocol is connectionless?",
         {"TCP", "UDP", "TLS", "FTP"}, 1},
        {"Which algorithm is used for deadlock avoidance?",
         {"Banker's algorithm", "Dijkstra's shortest path", "Huffman coding", "Peterson's CRC"}, 0},
        {"Before destroying a joinable std::thread you must...",
         {"Do nothing", "join() or detach() it", "Call sleep()", "Lock a mutex"}, 1},
        {"Which data structure supports iterative depth-first search?",
         {"Queue", "Stack", "Heap", "Hash table"}, 1},
        {"What is virtual memory?",
         {"An abstraction giving processes more address space than physical RAM", "A copy of the CPU cache", "A network drive", "GPU memory"}, 0},
        {"What must a primary key be?",
         {"Unique and non-null", "Always numeric", "Always auto-incremented", "A foreign key"}, 0},
        {"What is 7 % 3 in C++?",
         {"0", "1", "2", "3"}, 1},
        {"Which memory region stores local variables?",
         {"Heap", "Stack", "Code segment", "Data segment"}, 1},
        {"A semaphore with initial value 1 is typically used as...",
         {"A binary semaphore (mutex-like)", "A barrier", "A timer", "A pipe"}, 0},
        {"The producer-consumer pattern coordinates threads that...",
         {"Exchange data through a bounded buffer", "Sort arrays", "Compress files", "Route packets"}, 0},
        {"Which header provides std::counting_semaphore?",
         {"<mutex>", "<semaphore>", "<atomic>", "<thread>"}, 1},
        {"Worst-case lookup time in a hash table with many collisions?",
         {"O(1)", "O(log n)", "O(n)", "O(n log n)"}, 2},
        {"Which cache write policy writes to memory only when a line is evicted?",
         {"Write-through", "Write-back", "Write-around", "No-write-allocate"}, 1},
        {"What does join() on a std::thread do?",
         {"Blocks until the thread finishes", "Merges two threads", "Starts the thread", "Sets its priority"}, 0},
        {"Which sort is stable and O(n log n) in the worst case?",
         {"Quicksort", "Heapsort", "Merge sort", "Selection sort"}, 2},
        {"Which Git command creates a branch and switches to it?",
         {"git checkout -b <name>", "git branch -d <name>", "git merge <name>", "git stash"}, 0},
    };
}

// Thread-safe question bank: shared (read) lock for serving questions,
// exclusive (write) lock only when questions are added.
class QuestionBank {
public:
    QuestionBank() {
        for (auto& q : defaultQuestions()) add(std::move(q));
    }

    void add(Question q) {
        std::unique_lock lock(mutex_);
        q.id = static_cast<int>(questions_.size()) + 1;
        questions_.push_back(std::move(q));
    }

    std::size_t size() const {
        std::shared_lock lock(mutex_);
        return questions_.size();
    }

    // Returns n distinct random questions (copies, so no reference escapes the lock).
    std::vector<Question> randomSet(std::size_t n, std::mt19937& rng) const {
        std::shared_lock lock(mutex_);
        std::vector<std::size_t> idx(questions_.size());
        std::iota(idx.begin(), idx.end(), std::size_t{0});
        std::shuffle(idx.begin(), idx.end(), rng);
        n = std::min(n, idx.size());
        std::vector<Question> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) out.push_back(questions_[idx[i]]);
        return out;
    }

    // Optional extension: load "question|opt1|opt2|...|correctIndex(1-based)" lines.
    std::size_t loadFromFile(const std::string& path) {
        std::ifstream in(path);
        std::string line;
        std::size_t added = 0;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string tok;
            while (std::getline(ss, tok, '|')) parts.push_back(tok);
            if (parts.size() < 4) continue;  // text + >=2 options + answer
            int correct = 0;
            try { correct = std::stoi(parts.back()) - 1; } catch (...) { continue; }
            Question q;
            q.text = parts.front();
            q.options.assign(parts.begin() + 1, parts.end() - 1);
            if (correct < 0 || correct >= static_cast<int>(q.options.size())) continue;
            q.correct = correct;
            add(std::move(q));
            ++added;
        }
        return added;
    }

private:
    mutable std::shared_mutex mutex_;
    std::vector<Question> questions_;
};

// --------------------------------------------------------------- Scoreboard
enum class Status { Waiting, Running, Submitting, Finished };

inline const char* toString(Status s) {
    switch (s) {
        case Status::Waiting:    return "Waiting";
        case Status::Running:    return "Running";
        case Status::Submitting: return "Submitting";
        case Status::Finished:   return "Finished";
    }
    return "?";
}

struct StudentRecord {
    int id = 0;
    std::string name;
    int adminId = 0;
    int score = 0;
    int answered = 0;
    int total = 0;
    std::string currentQuestion;
    Status status = Status::Waiting;
    bool timedOut = false;
    double finishSeconds = 0.0;  // elapsed since the group's quiz started
};

// All mutation happens under one mutex => every update is atomic.
// Readers (the GUI) only ever receive a copy taken under the lock.
class Scoreboard {
public:
    int add(const std::string& name, int adminId, int total) {
        std::lock_guard lock(mutex_);
        StudentRecord r;
        r.id = nextId_++;
        r.name = name;
        r.adminId = adminId;
        r.total = total;
        r.currentQuestion = "(waiting to start)";
        int id = r.id;
        records_.emplace(id, std::move(r));
        return id;
    }

    void setStatus(int id, Status s) {
        std::lock_guard lock(mutex_);
        if (auto it = records_.find(id); it != records_.end()) it->second.status = s;
    }

    void setCurrent(int id, const std::string& text) {
        std::lock_guard lock(mutex_);
        if (auto it = records_.find(id); it != records_.end()) it->second.currentQuestion = text;
    }

    void recordAnswer(int id, bool correct) {
        std::lock_guard lock(mutex_);
        if (auto it = records_.find(id); it != records_.end()) {
            it->second.answered += 1;
            if (correct) it->second.score += 1;
        }
    }

    int score(int id) const {
        std::lock_guard lock(mutex_);
        auto it = records_.find(id);
        return it == records_.end() ? -1 : it->second.score;
    }

    void finish(int id, bool timedOut, double seconds) {
        std::lock_guard lock(mutex_);
        if (auto it = records_.find(id); it != records_.end()) {
            auto& r = it->second;
            r.status = Status::Finished;
            r.timedOut = timedOut;
            r.finishSeconds = seconds;
            r.currentQuestion = timedOut ? "(ended early: time up / stopped)" : "(completed)";
        }
    }

    void removeAdmin(int adminId) {
        std::lock_guard lock(mutex_);
        for (auto it = records_.begin(); it != records_.end();)
            it = (it->second.adminId == adminId) ? records_.erase(it) : std::next(it);
    }

    std::vector<StudentRecord> snapshot() const {
        std::lock_guard lock(mutex_);
        std::vector<StudentRecord> out;
        out.reserve(records_.size());
        for (const auto& [id, r] : records_) out.push_back(r);
        return out;
    }

private:
    mutable std::mutex mutex_;
    std::map<int, StudentRecord> records_;
    int nextId_ = 1;
};

// Ranking: score desc, finished before unfinished, faster finish first, id.
inline std::vector<StudentRecord> leaderboard(std::vector<StudentRecord> recs) {
    std::sort(recs.begin(), recs.end(), [](const StudentRecord& a, const StudentRecord& b) {
        if (a.score != b.score) return a.score > b.score;
        bool fa = a.status == Status::Finished, fb = b.status == Status::Finished;
        if (fa != fb) return fa;
        if (fa && a.finishSeconds != b.finishSeconds) return a.finishSeconds < b.finishSeconds;
        return a.id < b.id;
    });
    return recs;
}

// --------------------------------------------------------- Submission queue
struct Timing {
    int thinkMinMs = 800;   // per-question "thinking" delay
    int thinkMaxMs = 2500;
    int writeMinMs = 400;   // simulated database write
    int writeMaxMs = 1200;
};

class SubmissionQueue {
public:
    static constexpr int kPermits = 3;

    SubmissionQueue(int writeMinMs, int writeMaxMs, const std::string& logPath)
        : writeMin_(writeMinMs), writeMax_(std::max(writeMinMs, writeMaxMs)) {
        if (!logPath.empty()) file_.open(logPath, std::ios::app);
    }

    // Blocks until one of the 3 permits is free, then "writes" the result.
    void submit(const std::string& name, int studentId, int score, int total, bool timedOut) {
        waiting_.fetch_add(1);
        sem_.acquire();
        waiting_.fetch_sub(1);

        int now = active_.fetch_add(1) + 1;
        int p = peak_.load();
        while (now > p && !peak_.compare_exchange_weak(p, now)) {}

        thread_local std::mt19937 rng{std::random_device{}()};
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::uniform_int_distribution<int>(writeMin_, writeMax_)(rng)));

        writeLog(name, studentId, score, total, timedOut);

        active_.fetch_sub(1);
        total_.fetch_add(1);
        sem_.release();
    }

    int waiting() const { return waiting_.load(); }
    int active() const { return active_.load(); }
    int peakConcurrent() const { return peak_.load(); }
    int totalWritten() const { return total_.load(); }

    std::vector<std::string> recentLog() const {
        std::lock_guard lock(logMutex_);
        return {recent_.begin(), recent_.end()};
    }

private:
    void writeLog(const std::string& name, int id, int score, int total, bool timedOut) {
        std::lock_guard lock(logMutex_);  // prevents interleaved output
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream os;
        os << '[' << std::put_time(&tm, "%H:%M:%S") << "] student=" << name << " (id " << id
           << ") score=" << score << '/' << total << ' ' << (timedOut ? "TIMED_OUT" : "COMPLETED");
        if (file_.is_open()) file_ << os.str() << '\n' << std::flush;
        recent_.push_back(os.str());
        if (recent_.size() > 200) recent_.pop_front();
    }

    std::counting_semaphore<kPermits> sem_{kPermits};
    std::atomic<int> waiting_{0}, active_{0}, peak_{0}, total_{0};
    int writeMin_, writeMax_;
    mutable std::mutex logMutex_;
    std::ofstream file_;
    std::deque<std::string> recent_;
};

// -------------------------------------------------------------- Shared state
struct Shared {
    QuestionBank bank;
    Scoreboard board;
    SubmissionQueue queue;
    Timing timing;
    std::atomic<int> mismatches{0};  // score verification failures

    Shared(const Timing& t, const std::string& logPath)
        : queue(t.writeMinMs, t.writeMaxMs, logPath), timing(t) {}
};

// -------------------------------------------------------------------- Admin
struct QuizConfig {
    int students = 10;
    int questionsPerStudent = 8;
    double durationSec = 60.0;
};

class Admin {
public:
    enum class State { Idle, Running, Finished };

    Admin(int id, Shared& shared) : id_(id), shared_(shared) {}
    ~Admin() { stop(); wait(); }
    Admin(const Admin&) = delete;
    Admin& operator=(const Admin&) = delete;

    int id() const { return id_; }
    State state() const { return state_.load(); }
    bool running() const { return state() == State::Running; }

    // Starts (or restarts) this admin's group. Returns false if already running.
    bool start(const QuizConfig& cfg) {
        std::lock_guard lock(ctl_);
        if (state_.load() == State::Running) return false;
        if (thread_.joinable()) thread_.join();
        shared_.board.removeAdmin(id_);
        cfg_ = cfg;
        stop_.store(false);
        std::int64_t now = nowNs();
        startNs_.store(now);
        deadlineNs_.store(now + static_cast<std::int64_t>(cfg.durationSec * 1e9));
        state_.store(State::Running);
        thread_ = std::thread(&Admin::run, this);
        return true;
    }

    void stop() { stop_.store(true); }

    // Blocks until the current run (if any) has completed.
    void wait() {
        std::lock_guard lock(ctl_);
        if (thread_.joinable()) thread_.join();
    }

    double remainingSeconds() const {
        if (!running()) return 0.0;
        return std::max(0.0, (deadlineNs_.load() - nowNs()) / 1e9);
    }
    double durationSeconds() const { return (deadlineNs_.load() - startNs_.load()) / 1e9; }
    double fractionRemaining() const {
        double d = durationSeconds();
        return d > 0 ? std::min(1.0, remainingSeconds() / d) : 0.0;
    }

private:
    bool shouldStop() const { return stop_.load() || nowNs() >= deadlineNs_.load(); }

    // Sleeps in small slices so stop/timeout is noticed quickly. false => interrupted.
    bool interruptibleSleep(int ms) const {
        auto end = Clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            if (shouldStop()) return false;
            auto now = Clock::now();
            if (now >= end) return true;
            std::this_thread::sleep_for(
                std::min<Clock::duration>(std::chrono::milliseconds(20), end - now));
        }
    }

    void run() {
        const QuizConfig cfg = cfg_;
        const int total = static_cast<int>(
            std::min<std::size_t>(static_cast<std::size_t>(std::max(cfg.questionsPerStudent, 0)),
                                  shared_.bank.size()));
        std::random_device rd;
        std::vector<std::pair<int, std::string>> ids;
        for (int i = 0; i < cfg.students; ++i) {
            std::ostringstream n;
            n << 'A' << id_ << "-S" << std::setw(2) << std::setfill('0') << (i + 1);
            ids.emplace_back(shared_.board.add(n.str(), id_, total), n.str());
        }
        std::vector<std::thread> students;
        students.reserve(ids.size());
        for (const auto& [sid, name] : ids)
            students.emplace_back(&Admin::studentMain, this, sid, name,
                                  rd() ^ (static_cast<unsigned>(sid) * 2654435761u), total);
        for (auto& t : students) t.join();
        state_.store(State::Finished);
    }

    void studentMain(int sid, std::string name, unsigned seed, int total) {
        std::mt19937 rng(seed);
        auto questions = shared_.bank.randomSet(static_cast<std::size_t>(total), rng);
        const double skill = std::uniform_real_distribution<double>(0.40, 0.95)(rng);
        int myCorrect = 0;  // independent count used to verify the scoreboard
        bool timedOut = false;
        shared_.board.setStatus(sid, Status::Running);

        for (const auto& q : questions) {
            if (shouldStop()) { timedOut = true; break; }
            shared_.board.setCurrent(sid, q.text);
            int think = std::uniform_int_distribution<int>(
                shared_.timing.thinkMinMs,
                std::max(shared_.timing.thinkMinMs, shared_.timing.thinkMaxMs))(rng);
            if (!interruptibleSleep(think)) { timedOut = true; break; }

            int pick = q.correct;
            if (std::bernoulli_distribution(skill)(rng) == false && q.options.size() > 1) {
                do { pick = std::uniform_int_distribution<int>(
                         0, static_cast<int>(q.options.size()) - 1)(rng);
                } while (pick == q.correct);
            }
            bool ok = (pick == q.correct);
            if (ok) ++myCorrect;
            shared_.board.recordAnswer(sid, ok);
        }

        if (shared_.board.score(sid) != myCorrect) shared_.mismatches.fetch_add(1);

        shared_.board.setStatus(sid, Status::Submitting);
        shared_.board.setCurrent(sid, "(submitting result...)");
        shared_.queue.submit(name, sid, myCorrect, total, timedOut);

        shared_.board.finish(sid, timedOut, (nowNs() - startNs_.load()) / 1e9);
    }

    int id_;
    Shared& shared_;
    std::mutex ctl_;
    std::thread thread_;
    QuizConfig cfg_;
    std::atomic<bool> stop_{false};
    std::atomic<State> state_{State::Idle};
    std::atomic<std::int64_t> startNs_{0}, deadlineNs_{0};
};

// ------------------------------------------------------------------- Engine
struct Analytics {
    int students = 0, finished = 0, running = 0, timedOut = 0;
    int maxScore = 0, minScore = 0;
    double avgScore = 0.0;
    int queueWaiting = 0, queueActive = 0, peakConcurrentWrites = 0, totalWrites = 0;
    int mismatches = 0;
};

class Engine {
public:
    explicit Engine(int numAdmins, Timing timing = {}, const std::string& logPath = "submissions.log")
        : shared_(timing, logPath) {
        for (int i = 1; i <= numAdmins; ++i) admins_.push_back(std::make_unique<Admin>(i, shared_));
    }

    Shared& shared() { return shared_; }
    Scoreboard& board() { return shared_.board; }
    std::size_t adminCount() const { return admins_.size(); }
    Admin& admin(std::size_t i) { return *admins_[i]; }

    void startAll(const QuizConfig& cfg) { for (auto& a : admins_) a->start(cfg); }
    void stopAll() { for (auto& a : admins_) a->stop(); }
    void waitAll() { for (auto& a : admins_) a->wait(); }

    Analytics analytics(const std::vector<StudentRecord>& snap) const {
        Analytics a;
        a.students = static_cast<int>(snap.size());
        long sum = 0;
        bool first = true;
        for (const auto& r : snap) {
            if (r.status == Status::Running || r.status == Status::Submitting) ++a.running;
            if (r.status != Status::Finished) continue;
            ++a.finished;
            if (r.timedOut) ++a.timedOut;
            sum += r.score;
            if (first) { a.maxScore = a.minScore = r.score; first = false; }
            a.maxScore = std::max(a.maxScore, r.score);
            a.minScore = std::min(a.minScore, r.score);
        }
        a.avgScore = a.finished ? static_cast<double>(sum) / a.finished : 0.0;
        a.queueWaiting = shared_.queue.waiting();
        a.queueActive = shared_.queue.active();
        a.peakConcurrentWrites = shared_.queue.peakConcurrent();
        a.totalWrites = shared_.queue.totalWritten();
        a.mismatches = shared_.mismatches.load();
        return a;
    }

private:
    Shared shared_;
    std::vector<std::unique_ptr<Admin>> admins_;
};

}  // namespace quiz
