// Dear ImGui + GLFW + OpenGL3 front end for the Multi-threaded Online Quiz System.
// The GUI thread never touches shared state directly: it only reads a scoreboard
// snapshot (copied under the mutex) and atomic counters, so it cannot block students.
#include "quiz_core.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace quiz;

static ImVec4 statusColor(const StudentRecord& r) {
    switch (r.status) {
        case Status::Waiting:    return {0.60f, 0.60f, 0.60f, 1.f};
        case Status::Running:    return {0.35f, 0.85f, 0.45f, 1.f};
        case Status::Submitting: return {0.95f, 0.80f, 0.25f, 1.f};
        case Status::Finished:   return r.timedOut ? ImVec4{0.95f, 0.50f, 0.35f, 1.f}
                                                   : ImVec4{0.45f, 0.70f, 1.00f, 1.f};
    }
    return {1, 1, 1, 1};
}

static std::string statusText(const StudentRecord& r) {
    if (r.status == Status::Finished) return r.timedOut ? "Timed out" : "Finished";
    return toString(r.status);
}

static std::string mmss(double sec) {
    int s = static_cast<int>(sec + 0.5);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", s / 60, s % 60);
    return buf;
}

struct AdminUi {
    int students = 8;
    int questions = 8;
    int duration = 45;  // seconds
};

static void drawAdminPanel(Engine& engine, std::vector<AdminUi>& ui, int maxQuestions) {
    if (ImGui::Button("Start all groups")) {
        for (std::size_t i = 0; i < engine.adminCount(); ++i)
            engine.admin(i).start({ui[i].students, ui[i].questions, double(ui[i].duration)});
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop all groups")) engine.stopAll();

    for (std::size_t i = 0; i < engine.adminCount(); ++i) {
        Admin& a = engine.admin(i);
        ImGui::PushID(static_cast<int>(i));
        ImGui::Spacing();
        ImGui::SeparatorText(("Admin " + std::to_string(a.id())).c_str());

        const bool running = a.running();
        ImGui::BeginDisabled(running);
        ImGui::SliderInt("Students", &ui[i].students, 1, 60);
        ImGui::SliderInt("Questions", &ui[i].questions, 1, maxQuestions);
        ImGui::SliderInt("Duration (s)", &ui[i].duration, 5, 300);
        ImGui::EndDisabled();

        if (!running) {
            if (ImGui::Button("Start quiz"))
                a.start({ui[i].students, ui[i].questions, double(ui[i].duration)});
        } else {
            if (ImGui::Button("Stop group")) a.stop();
        }
        ImGui::SameLine();
        const char* st = running ? "Running" : (a.state() == Admin::State::Finished ? "Finished" : "Idle");
        ImGui::TextUnformatted(st);

        float frac = running ? static_cast<float>(a.fractionRemaining()) : 0.f;
        std::string label = running ? mmss(a.remainingSeconds()) + " left" : "--:--";
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), label.c_str());
        ImGui::PopID();
    }
}

static void drawProgress(const std::vector<StudentRecord>& snap) {
    if (snap.empty()) { ImGui::TextDisabled("No quiz running. Start a group from the Admin Control panel."); return; }
    if (ImGui::BeginTable("progress", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Student", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Current question", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableHeadersRow();
        for (const auto& r : snap) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(statusColor(r), "%s", statusText(r).c_str());
            ImGui::TableNextColumn();
            float f = r.total ? float(r.answered) / float(r.total) : 0.f;
            char ov[24];
            std::snprintf(ov, sizeof ov, "%d/%d", r.answered, r.total);
            ImGui::ProgressBar(f, ImVec2(-FLT_MIN, 0), ov);
            ImGui::TableNextColumn(); ImGui::Text("%d", r.score);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.currentQuestion.c_str());
            ImGui::TableNextColumn();
            if (r.status == Status::Finished) ImGui::Text("%.1fs", r.finishSeconds);
            else ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }
}

static void drawLeaderboard(const std::vector<StudentRecord>& snap) {
    auto lb = leaderboard(snap);
    if (lb.empty()) { ImGui::TextDisabled("Waiting for students..."); return; }
    if (ImGui::BeginTable("leaderboard", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28);
        ImGui::TableSetupColumn("Student", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Done", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 75);
        ImGui::TableHeadersRow();
        int rank = 0;
        for (const auto& r : lb) {
            ++rank;
            ImGui::TableNextRow();
            ImVec4 gold{1.f, 0.85f, 0.2f, 1.f}, silver{0.8f, 0.8f, 0.85f, 1.f}, bronze{0.85f, 0.55f, 0.3f, 1.f};
            ImVec4 col = rank == 1 ? gold : rank == 2 ? silver : rank == 3 ? bronze : ImVec4{1, 1, 1, 1};
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%d", rank);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%s", r.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%d", r.score);
            ImGui::TableNextColumn(); ImGui::Text("%d/%d", r.answered, r.total);
            ImGui::TableNextColumn(); ImGui::TextColored(statusColor(r), "%s", statusText(r).c_str());
        }
        ImGui::EndTable();
    }
}

static void drawAnalytics(Engine& engine, const std::vector<StudentRecord>& snap) {
    Analytics a = engine.analytics(snap);
    ImGui::Text("Students: %d   Running: %d   Finished: %d   Timed out: %d",
                a.students, a.running, a.finished, a.timedOut);
    ImGui::Separator();
    ImGui::Text("Score  max: %d   min: %d   avg: %.2f", a.maxScore, a.minScore, a.avgScore);
    ImGui::Separator();
    ImGui::Text("Submission queue (semaphore, %d permits)", SubmissionQueue::kPermits);
    ImGui::BulletText("Waiting: %d", a.queueWaiting);
    ImGui::BulletText("Writing now: %d", a.queueActive);
    ImGui::BulletText("Peak concurrent writes: %d", a.peakConcurrentWrites);
    ImGui::BulletText("Total written: %d", a.totalWrites);
    ImGui::Separator();
    if (a.mismatches == 0) ImGui::TextColored({0.4f, 0.9f, 0.5f, 1.f}, "Score verification: OK (0 mismatches)");
    else ImGui::TextColored({1.f, 0.4f, 0.3f, 1.f}, "Score verification: %d MISMATCHES", a.mismatches);

    // Score histogram of finished students.
    int maxTotal = 0;
    for (const auto& r : snap) maxTotal = std::max(maxTotal, r.total);
    std::vector<float> bins(static_cast<std::size_t>(maxTotal) + 1, 0.f);
    for (const auto& r : snap)
        if (r.status == Status::Finished && r.score >= 0 && r.score <= maxTotal) bins[r.score] += 1.f;
    ImGui::Text("Score distribution (finished students)");
    ImGui::PlotHistogram("##hist", bins.data(), static_cast<int>(bins.size()), 0, nullptr, 0.f,
                         FLT_MAX, ImVec2(-FLT_MIN, 70));
}

static void drawLog(Engine& engine) {
    auto lines = engine.shared().queue.recentLog();
    ImGui::BeginChild("logscroll", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const auto& l : lines) ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

int main(int argc, char** argv) {
    int numAdmins = 3;
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--admins")) numAdmins = std::clamp(std::atoi(argv[i + 1]), 1, 8);

    if (!glfwInit()) { std::fprintf(stderr, "Failed to initialise GLFW\n"); return 1; }
#if defined(__APPLE__)
    const char* glsl = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    GLFWwindow* window = glfwCreateWindow(1500, 880, "Multi-threaded Online Quiz System", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.f;
    ImGui::GetStyle().FrameRounding = 4.f;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl);

    Engine engine(numAdmins);
    engine.shared().bank.loadFromFile("questions.txt");  // optional extra questions
    const int maxQuestions = static_cast<int>(engine.shared().bank.size());
    std::vector<AdminUi> ui(engine.adminCount());
    for (auto& u : ui) u.questions = std::min(u.questions, maxQuestions);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // One consistent snapshot per frame, taken under the scoreboard mutex.
        const std::vector<StudentRecord> snap = engine.board().snapshot();

        const ImVec2 W = ImGui::GetMainViewport()->WorkSize;
        const ImVec2 O = ImGui::GetMainViewport()->WorkPos;
        const float pad = 8.f;
        const float colL = W.x * 0.27f, colR = W.x * 0.25f, colM = W.x - colL - colR - 4 * pad;
        const float topH = W.y * 0.64f;

        ImGui::SetNextWindowPos({O.x + pad, O.y + pad}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({colL, W.y * 0.58f}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Admin Control");
        drawAdminPanel(engine, ui, maxQuestions);
        ImGui::End();

        ImGui::SetNextWindowPos({O.x + pad, O.y + W.y * 0.58f + 2 * pad}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({colL, W.y * 0.42f - 3 * pad}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Analytics");
        drawAnalytics(engine, snap);
        ImGui::End();

        ImGui::SetNextWindowPos({O.x + colL + 2 * pad, O.y + pad}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({colM, topH}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Live Progress");
        drawProgress(snap);
        ImGui::End();

        ImGui::SetNextWindowPos({O.x + colL + 2 * pad, O.y + topH + 2 * pad}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({colM, W.y - topH - 3 * pad}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Submission Log");
        drawLog(engine);
        ImGui::End();

        ImGui::SetNextWindowPos({O.x + colL + colM + 3 * pad, O.y + pad}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({colR, W.y - 2 * pad}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Leaderboard");
        drawLeaderboard(snap);
        ImGui::End();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.09f, 0.11f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    engine.stopAll();
    engine.waitAll();  // join every admin/student thread before tearing down

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
