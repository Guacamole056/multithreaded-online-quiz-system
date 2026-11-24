// Headless stress test for the concurrency core.
// Verifies: score accuracy, semaphore limit, completion, timeouts, stop, bank thread-safety.
#include "quiz_core.hpp"

#include <cstdio>
#include <set>

using namespace quiz;

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) std::printf("  [PASS] %s\n", msg);                  \
        else { std::printf("  [FAIL] %s\n", msg); ++failures; }       \
    } while (0)

static void testNormalRun() {
    std::puts("Test 1: 4 admins x 30 students, normal completion");
    Timing t{2, 10, 10, 30};
    Engine e(4, t, "stress_test.log");
    QuizConfig cfg{30, 10, 60.0};
    e.startAll(cfg);
    e.waitAll();

    auto snap = e.board().snapshot();
    auto a = e.analytics(snap);
    bool allDone = true, noneTimedOut = true, inRange = true, answeredAll = true;
    for (auto& r : snap) {
        allDone &= (r.status == Status::Finished);
        noneTimedOut &= !r.timedOut;
        inRange &= (r.score >= 0 && r.score <= r.total);
        answeredAll &= (r.answered == r.total);
    }
    CHECK(snap.size() == 120, "120 student records exist");
    CHECK(allDone, "every student thread finished and is marked Finished");
    CHECK(noneTimedOut, "no student timed out");
    CHECK(answeredAll, "every student answered all questions");
    CHECK(inRange, "all scores within [0, total]");
    CHECK(a.mismatches == 0, "scoreboard matches each student's independent count (0 mismatches)");
    CHECK(a.peakConcurrentWrites <= SubmissionQueue::kPermits, "peak concurrent writes <= 3");
    CHECK(a.peakConcurrentWrites >= 1, "queue was actually used");
    CHECK(a.totalWrites == 120, "exactly 120 submissions written");
    CHECK(a.queueActive == 0 && a.queueWaiting == 0, "queue drained");
    std::printf("  peak concurrent writes = %d, avg score = %.2f\n", a.peakConcurrentWrites, a.avgScore);
    auto lb = leaderboard(snap);
    bool sorted = true;
    for (std::size_t i = 1; i < lb.size(); ++i) sorted &= lb[i - 1].score >= lb[i].score;
    CHECK(sorted, "leaderboard sorted by score");
}

static void testTimeout() {
    std::puts("Test 2: countdown expiry ends quizzes early");
    Timing t{100, 200, 5, 15};
    Engine e(2, t, "stress_test.log");
    e.startAll(QuizConfig{15, 40, 1.0});  // 40 questions * >=100ms >> 1s
    e.waitAll();
    auto snap = e.board().snapshot();
    auto a = e.analytics(snap);
    bool allTimedOut = true, allDone = true;
    for (auto& r : snap) { allTimedOut &= r.timedOut; allDone &= r.status == Status::Finished; }
    CHECK(allDone, "all students finished");
    CHECK(allTimedOut, "all students marked timed out");
    CHECK(a.mismatches == 0, "scores still accurate after timeout");
    CHECK(a.peakConcurrentWrites <= SubmissionQueue::kPermits, "peak concurrent writes <= 3");
}

static void testAdminStop() {
    std::puts("Test 3: admin stops one group mid-quiz, the other continues");
    Timing t{20, 40, 10, 20};
    Engine e(2, t, "stress_test.log");
    QuizConfig cfg{10, 30, 60.0};
    e.admin(0).start(cfg);
    e.admin(1).start(cfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    e.admin(0).stop();
    e.waitAll();
    auto snap = e.board().snapshot();
    int stoppedTimedOut = 0, otherTimedOut = 0;
    for (auto& r : snap) {
        if (r.adminId == 1 && r.timedOut) ++stoppedTimedOut;
        if (r.adminId == 2 && r.timedOut) ++otherTimedOut;
    }
    // Group 2 needs 30 * ~30ms = ~900ms+, so it normally completes before the 60s deadline.
    CHECK(stoppedTimedOut == 10, "stopped group: all 10 students ended early");
    CHECK(otherTimedOut == 0, "other group unaffected");
    CHECK(e.analytics(snap).mismatches == 0, "scores accurate");

    // Restart of a group replaces its old records.
    e.admin(0).start(QuizConfig{5, 5, 60.0});
    e.admin(0).wait();
    int a1 = 0;
    for (auto& r : e.board().snapshot()) a1 += (r.adminId == 1);
    CHECK(a1 == 5, "restarting a group replaces its previous records");
}

static void testBankThreadSafety() {
    std::puts("Test 4: question bank under concurrent readers and writers");
    QuestionBank bank;
    const std::size_t initial = bank.size();
    std::atomic<bool> bad{false};
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i)
        ts.emplace_back([&, i] {
            std::mt19937 rng(i + 1);
            for (int k = 0; k < 2000; ++k) {
                auto set = bank.randomSet(10, rng);
                std::set<int> ids;
                for (auto& q : set) {
                    ids.insert(q.id);
                    if (q.correct < 0 || q.correct >= (int)q.options.size()) bad = true;
                }
                if (set.size() != 10 || ids.size() != 10) bad = true;
            }
        });
    for (int w = 0; w < 2; ++w)
        ts.emplace_back([&] {
            for (int k = 0; k < 100; ++k) bank.add(Question{"extra", {"a", "b"}, 0});
        });
    for (auto& t : ts) t.join();
    CHECK(!bad.load(), "all random sets valid (10 distinct questions, valid answers)");
    CHECK(bank.size() == initial + 200, "all concurrent additions recorded");
}

int main() {
    auto start = Clock::now();
    testNormalRun();
    testTimeout();
    testAdminStop();
    testBankThreadSafety();
    double s = std::chrono::duration<double>(Clock::now() - start).count();
    std::printf("\n%s (%.1fs, %d failure%s)\n", failures ? "FAILED" : "ALL TESTS PASSED", s, failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
