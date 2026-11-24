# Multi-threaded Online Quiz System

Concurrent exam platform with a live leaderboard (C++20, Dear ImGui, GLFW, OpenGL, CMake).
Project members: 24K-2027 Rohaan Zaidi, 24K-2037 Hassan Arif.

## Layout

| Path | Purpose |
| --- | --- |
| `include/quiz_core.hpp` | Header-only concurrency core (no GUI dependency) |
| `src/main.cpp` | Dear ImGui desktop application |
| `tests/stress_test.cpp` | Headless stress / validation test |
| `questions.txt` | Optional extra questions loaded by the GUI at start-up |
| `CMakeLists.txt` | Build (fetches GLFW 3.4 and Dear ImGui 1.91.9 automatically) |

## Build and run

Requirements: a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022), CMake 3.16+, git, and OpenGL.
On Debian/Ubuntu: `sudo apt install build-essential cmake git libgl1-mesa-dev xorg-dev`.

```
cmake -S . -B build
sudo apt update
sudo apt install wayland-protocols libwayland-dev
cmake --build build -j$(nproc)
./build/quiz_gui   #run from the root, --admins 4 (optional)
ctest --test-dir build --output-on-failure
```
Options: `-DQUIZ_BUILD_GUI=OFF` (core + tests only, no OpenGL needed), `-DQUIZ_BUILD_TESTS=OFF`.
On Windows (Visual Studio) use `cmake --build build --config Release`; the exe is in `build/Release`.

## Using the GUI

1. In **Admin Control**, set students, questions and duration per admin, then press **Start quiz**
   (or **Start all groups**). **Stop group** ends that admin's students early.
2. **Live Progress** shows each student's status, progress, score and the question being answered.
3. **Leaderboard** re-ranks continuously (score, then finish time).
4. **Analytics** shows max/min/avg score, finished count, queue waiting/active, peak concurrent
   writes, and the score-verification result. **Submission Log** mirrors `submissions.log`.

## Synchronization design (matches the proposal)

| Shared resource | Mechanism | Purpose |
| --- | --- | --- |
| Question bank | `std::shared_mutex` (shared lock for reads) | Many students read concurrently |
| Scoreboard | `std::mutex` | Every score/progress update is atomic |
| Submission queue | `std::counting_semaphore<3>` | At most 3 simultaneous "database" writes |
| Submission log | `std::mutex` | No interleaved log lines |
| Statistics | `std::atomic` | Lock-free queue length / peak concurrent writes |

The GUI only reads a copy of the scoreboard taken under the lock, so rendering never blocks student
threads and never sees a half-updated state.

Thread model: one **admin thread** per admin; each spawns one **student thread** per student and
joins them. Students check a stop flag and the deadline between questions and during their "thinking"
delay, so countdown expiry or *Stop group* ends the quiz promptly (status *Timed out*), after which the
partial score is still submitted through the semaphore-controlled queue.

## Testing (`tests/stress_test.cpp`)

1. 4 admins x 30 students: all finish, scoreboard equals each student's independent correct-answer
   count (0 mismatches), peak concurrent writes <= 3, exactly 120 submissions written.
2. Countdown expiry: every student ends early and is marked timed out; scores still accurate.
3. Admin stops one group mid-quiz while another continues; restarting replaces old records.
4. Question bank under 8 concurrent readers + 2 writers: always valid, distinct question sets.

The suite was also run under ThreadSanitizer (`-fsanitize=thread`) with no data races reported.

## Extending

Add questions at runtime in `questions.txt` using `question|option1|option2|...|correct (1-based)`.
Timing (thinking/write delays) is configured via the `Timing` struct passed to `Engine`.
