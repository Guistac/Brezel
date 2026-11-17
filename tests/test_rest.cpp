// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// The rest: the command stack, time (the raw monotonic clock, the remote clock estimate), StringID, Signal, the fitting
// math, the console and its visitors, the Application namespace, the Dispatcher alias.
// Headers: Command/Command.hpp, Command/CommandStack.hpp, Foundation/MonotonicRawClock.hpp,
// Time/RemoteClockEstimator.hpp, Foundation/StringID.hpp, Foundation/Signal.hpp, Math/LeastSquares.hpp,
// Math/LineFit.hpp, Math/CircleFit.hpp, Console/Console.hpp, Console/SearchVisitor.hpp,
// Serialization/ConsoleVisitor.hpp, App/Application.hpp, Core/Dispatcher.hpp.

#include "BrezelTest.hpp"
#include "TestComponents.hpp"

#include "Brezel/App/Application.hpp"
#include "Brezel/Command/Command.hpp"
#include "Brezel/Command/CommandStack.hpp"
#include "Brezel/Console/Console.hpp"
#include "Brezel/Console/SearchVisitor.hpp"
#include "Brezel/Core/Dispatcher.hpp"
#include "Brezel/Foundation/MonotonicRawClock.hpp"
#include "Brezel/Foundation/Signal.hpp"
#include "Brezel/Foundation/StringID.hpp"
#include "Brezel/Math/CircleFit.hpp"
#include "Brezel/Math/LeastSquares.hpp"
#include "Brezel/Math/LineFit.hpp"
#include "Brezel/Serialization/ConsoleVisitor.hpp"
#include "Brezel/Time/RemoteClockEstimator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace Brezel;
using namespace BrezelTest;
using namespace std::chrono_literals;

namespace {

/// A command that sets an int and restores the old value
class SetInt : public Command {
public:
    SetInt(int& target, int value, int& executions) : m_target(target), m_value(value), m_executions(executions) {}
    void execute() override {
        m_old = m_target;
        m_target = m_value;
        ++m_executions;
    }
    void undo() override { m_target = m_old; }
    std::string getDescription() const override { return "set to " + std::to_string(m_value); }

private:
    int& m_target;
    int m_value;
    int m_old{0};
    int& m_executions;
};

/// A component with an action and a read-only property, for the console
struct Switch {
    int count{1};
    bool on{true};
};
template <typename V>
void reflect(Switch& s, V& v) {
    v.visit_property("count", s.count, {Tag::Persistent, Tag::ReadOnly});
    v.visit_property("on", s.on, {Tag::Persistent});
    v.visit_action("reset", [&s] { s.count = 0; });
}

/// Captures std::cout while it lives
struct CoutCapture {
    std::ostringstream buffer;
    std::streambuf* old;
    CoutCapture() : old(std::cout.rdbuf(buffer.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old); }
};

/// Sets BREZEL_ECS_WALL=count for the projects constructed while it lives
struct CountModeEnvironment {
    CountModeEnvironment() { ::setenv("BREZEL_ECS_WALL", "count", 1); }
    ~CountModeEnvironment() { ::unsetenv("BREZEL_ECS_WALL"); }
};

uint64_t fnv1aBytes(const unsigned char* s, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<uint64_t>(s[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

template <typename S>
constexpr bool hasDisconnect = requires(S& s) { s.disconnect(); };

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Command stack
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(command_stack_undo_redo, "Command/CommandStack.hpp",
            "pushAndExecute runs the command and clears the redo list; undo and redo move one command across and run "
            "it; both do nothing when their list is empty; each project owns a stack, also published in its registry "
            "context") {
    int value = 0;
    int executions = 0;
    CommandStack stack;
    stack.undo();
    stack.redo();
    stack.pushAndExecute(std::make_unique<SetInt>(value, 1, executions));
    stack.pushAndExecute(std::make_unique<SetInt>(value, 2, executions));
    CHECK_EQ(value, 2);
    stack.undo();
    CHECK_EQ(value, 1);
    stack.redo();
    CHECK_EQ(value, 2);
    CHECK_EQ(executions, 3);
    stack.undo();
    stack.undo();
    CHECK_EQ(value, 0);
    stack.redo();
    CHECK_EQ(value, 1);
    stack.pushAndExecute(std::make_unique<SetInt>(value, 5, executions));
    stack.redo();  // the redo list was cleared by the push
    CHECK_EQ(value, 5);
    SetInt described(value, 7, executions);
    CHECK_EQ(described.getDescription(), std::string("set to 7"));

    TestWorld world("Rest", false);
    auto& ctx = world.project.write().registry().ctx();
    CHECK(ctx.get<CommandStack*>() == &world.project.getStack());
    CHECK(ctx.get<EcsWall*>() == &world.project.ecsWall());
}

// ---------------------------------------------------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(monotonic_raw_clock, "Foundation/MonotonicRawClock.hpp",
            "monotonicRawNanoseconds never goes back and advances with real time") {
    int64_t last = monotonicRawNanoseconds();
    bool monotonic = true;
    for (int i = 0; i < 100000; ++i) {
        const int64_t now = monotonicRawNanoseconds();
        if (now < last) monotonic = false;
        last = now;
    }
    CHECK(monotonic);
    const int64_t before = monotonicRawNanoseconds();
    std::this_thread::sleep_for(5ms);
    CHECK(monotonicRawNanoseconds() - before >= 4'000'000);
}

BREZEL_TEST(remote_clock_estimator, "Time/RemoteClockEstimator.hpp",
            "the offset is the smallest local minus remote over a 5 s window, valid once at least 5 samples span at "
            "least 1 s; a sample older than the window is ignored; old minima leave the window; reset forgets; a "
            "wrapping 32-bit microsecond stamp is unwrapped near the reference") {
    using RCE = RemoteClockEstimator;
    RCE est;
    const uint64_t base = 1'000'000'000'000ULL;
    const int64_t offset = 2'500'000;  // local is ahead by 2.5 ms
    // four samples over 2 s: too few
    for (int i = 0; i < 4; ++i) est.addSample(base + i * 500'000'000ULL, base + i * 500'000'000ULL + offset + 100'000);
    CHECK(!est.hasEstimate());
    CHECK_EQ(est.offsetNs(), int64_t(0));
    est.reset();
    // five samples within 0.5 s: span too short
    for (int i = 0; i < 5; ++i) est.addSample(base + i * 100'000'000ULL, base + i * 100'000'000ULL + offset);
    CHECK(!est.hasEstimate());
    est.reset();
    // eleven samples over 1 s, transit 100 to 1000 us, one fast one: the minimum wins
    for (int i = 0; i <= 10; ++i) {
        const uint64_t remote = base + i * 100'000'000ULL;
        const int64_t transit = (i == 6) ? 50'000 : 100'000 + i * 90'000;
        est.addSample(remote, remote + offset + transit);
    }
    REQUIRE(est.hasEstimate());
    CHECK_EQ(est.offsetNs(), offset + 50'000);
    CHECK_EQ(est.toLocalNs(base), base + static_cast<uint64_t>(offset + 50'000));
    // a sample older than the window is ignored, even with a smaller difference
    est.addSample(base - 10'000'000'000ULL, base - 10'000'000'000ULL + offset - 1'000'000);
    CHECK_EQ(est.offsetNs(), offset + 50'000);
    // six seconds later the fast sample has left the window
    for (int i = 0; i <= 10; ++i) {
        const uint64_t remote = base + 7'000'000'000ULL + i * 100'000'000ULL;
        est.addSample(remote, remote + offset + 300'000);
    }
    CHECK_EQ(est.offsetNs(), offset + 300'000);
    est.reset();
    CHECK(!est.hasEstimate());

    // unwrap: a stamp 10 us before the reference, across the 32-bit wrap
    RCE zero;
    const uint64_t referenceUs = (1ULL << 32) + 5;
    const uint32_t stamp = static_cast<uint32_t>(referenceUs - 10);
    CHECK_EQ(zero.remoteNsFromWrappedUs(stamp, referenceUs * 1000ULL), (referenceUs - 10) * 1000ULL);
    const uint32_t after = static_cast<uint32_t>(referenceUs + 20);
    CHECK_EQ(zero.remoteNsFromWrappedUs(after, referenceUs * 1000ULL), (referenceUs + 20) * 1000ULL);
}

// ---------------------------------------------------------------------------------------------------------------------
// StringID, Signal, Dispatcher
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(string_id, "Foundation/StringID.hpp",
            "StringID is a 64-bit FNV-1a hash computed at compile time or run time; from() and the implicit const "
            "char* constructor store the text for toString(), the _sid literal does not; the default is invalid; "
            "fmt and std::hash use it; two threads interning at once lose nothing") {
    static_assert(StringID::hash_string("a") == 0xaf63dc4c8601ec8cULL);
    static_assert(StringID::hash_string("") == StringID::FNV_OFFSET_BASIS);
    static_assert(StringID::hash_string_view("abc") == StringID::hash_string("abc"));
    CHECK(!StringID().isValid());
    const StringID a = StringID::from("brezel.test.from");
    CHECK(a.isValid());
    CHECK_EQ(a.toString(), std::string("brezel.test.from"));
    const StringID implicitId = "brezel.test.implicit";
    CHECK_EQ(implicitId.toString(), std::string("brezel.test.implicit"));
    constexpr StringID literal = "brezel.test.literal.never.registered"_sid;
    CHECK(literal == StringID(StringID::hash_string("brezel.test.literal.never.registered")));
    CHECK_EQ(literal.toString(), "Unknown_SID_" + std::to_string(literal.hash()));
    CHECK_EQ(fmt::format("{}", a), std::string("brezel.test.from"));
    CHECK_EQ(std::hash<StringID>{}(a), static_cast<size_t>(a.hash()));
    CHECK(StringID::from("x") == StringID::from(std::string("x")));
    CHECK(StringID::from("x") != StringID::from("y"));

    Watchdog watchdog("string id threads", 20s);
    {
        ScopedThread t1([](const std::atomic<bool>&) {
            for (int i = 0; i < 2000; ++i) StringID::from("brezel.thread.one." + std::to_string(i));
        });
        ScopedThread t2([](const std::atomic<bool>&) {
            for (int i = 0; i < 2000; ++i) StringID::from("brezel.thread.two." + std::to_string(i));
        });
    }
    bool all = true;
    for (int i = 0; i < 2000; i += 97) {
        const std::string one = "brezel.thread.one." + std::to_string(i);
        const std::string two = "brezel.thread.two." + std::to_string(i);
        all = all && StringID(StringID::hash_string(one.c_str())).toString() == one;
        all = all && StringID(StringID::hash_string(two.c_str())).toString() == two;
    }
    CHECK(all);
}

BREZEL_TEST(known_defect_new2_string_id_char_sign, "Foundation/StringID.hpp",
            "StringID hashes each char through its signed or unsigned value, so a label with a byte at or above 0x80 "
            "hashes differently where char is signed (x86-64, Apple arm64) and where it is unsigned (arm64 Linux); "
            "UUID::deriveFromString hashes bytes and does not (new finding)") {
    const char text[] = "\xC3\xA9";  // one non-ASCII character
    const uint64_t byteHash = fnv1aBytes(reinterpret_cast<const unsigned char*>(text), 2);
    const uint64_t sidHash = StringID::hash_string(text);
    if (std::is_signed_v<char>) {
        CHECK(sidHash != byteHash);
        std::printf("    char is signed here: the StringID hash of a non-ASCII label differs from an unsigned-char platform\n");
    } else {
        CHECK_EQ(sidHash, byteHash);
        std::printf("    char is unsigned here: the StringID hash differs from a signed-char platform\n");
    }
    CHECK_EQ(UUID::deriveFromString(text).value, byteHash);
    CHECK_EQ(StringID::hash_string("ascii"), fnv1aBytes(reinterpret_cast<const unsigned char*>("ascii"), 5));
}

BREZEL_TEST(signal_connect_emit, "Foundation/Signal.hpp",
            "emit calls every connected slot in connection order with the arguments; there is no disconnect") {
    static_assert(!hasDisconnect<Signal<int>>);
    Signal<int, std::string> signal;
    std::vector<std::string> calls;
    signal.connect([&](int n, std::string s) { calls.push_back("first " + std::to_string(n) + s); });
    signal.connect([&](int n, std::string s) { calls.push_back("second " + std::to_string(n) + s); });
    signal.emit(3, "x");
    CHECK((calls == std::vector<std::string>{"first 3x", "second 3x"}));
    static_assert(std::is_same_v<Dispatcher, entt::dispatcher>);
}

// ---------------------------------------------------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(math_least_squares, "Math/LeastSquares.hpp",
            "LeastSquares fits exact data exactly and noisy data to the least-squares minimum with its RMS residual; "
            "fewer rows than unknowns, a rank-deficient problem, a non-finite value or a weight that is not positive "
            "give ok false; an ill-conditioned problem is solved by the QR path") {
    Math::LeastSquares<2> exact;
    for (int t = 0; t < 10; ++t) exact.addRow({1.0, double(t)}, 2.0 + 3.0 * t);
    auto sol = exact.solve();
    REQUIRE(sol.ok);
    CHECK_NEAR(sol.x[0], 2.0, 1e-9);
    CHECK_NEAR(sol.x[1], 3.0, 1e-9);
    CHECK_NEAR(sol.residualRms, 0.0, 1e-9);
    CHECK(!sol.usedQr);
    CHECK_EQ(sol.rows, size_t(10));

    Math::LeastSquares<2> noisy;
    noisy.addRow({1.0, 0.0}, 0.0);
    noisy.addRow({1.0, 1.0}, 1.0);
    noisy.addRow({1.0, 2.0}, 0.0);
    sol = noisy.solve();
    REQUIRE(sol.ok);
    CHECK_NEAR(sol.x[0], 1.0 / 3.0, 1e-12);
    CHECK_NEAR(sol.x[1], 0.0, 1e-12);
    CHECK_NEAR(sol.residualRms, std::sqrt(2.0 / 9.0), 1e-12);

    Math::LeastSquares<2> few;
    few.addRow({1.0, 1.0}, 1.0);
    CHECK(!few.solve().ok);
    Math::LeastSquares<2> deficient;
    for (int i = 0; i < 5; ++i) deficient.addRow({1.0, 1.0}, double(i));
    CHECK(!deficient.solve().ok);
    Math::LeastSquares<1> nonFinite;
    nonFinite.addRow({1.0}, 1.0);
    nonFinite.addRow({std::numeric_limits<double>::quiet_NaN()}, 1.0);
    CHECK(!nonFinite.solve().ok);
    Math::LeastSquares<1> badWeight;
    badWeight.addRow({1.0}, 1.0);
    badWeight.addRow({1.0}, 1.0, 0.0);
    CHECK(!badWeight.solve().ok);
    badWeight.reset();
    badWeight.addRow({2.0}, 4.0);
    CHECK_NEAR(badWeight.solve().x[0], 2.0, 1e-12);

    Math::LeastSquares<2> illConditioned;  // the second column is the first plus 1e-8 * t
    for (int t = -50; t <= 50; ++t) illConditioned.addRow({1.0, 1.0 + 1e-8 * t}, 3.0 + 2.0 * (1.0 + 1e-8 * t));
    sol = illConditioned.solve();
    REQUIRE(sol.ok);
    CHECK(sol.usedQr);
    CHECK_NEAR(sol.x[0], 3.0, 1e-4);
    CHECK_NEAR(sol.x[1], 2.0, 1e-4);
}

BREZEL_TEST(math_line_fit, "Math/LineFit.hpp",
            "the total least-squares line through points on a line has that direction (sign arbitrary), RMS 0, the "
            "centroid on it and the spread along it; fewer than 2 points, equal points or a non-finite point give ok "
            "false") {
    const double xs[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    const double ys[] = {1.0, 3.0, 5.0, 7.0, 9.0};
    auto fit = Math::fitLine2D(xs, ys, 5);
    REQUIRE(fit.ok);
    const double inv = 1.0 / std::sqrt(5.0);
    CHECK_NEAR(std::fabs(fit.direction[0]), inv, 1e-12);
    CHECK_NEAR(std::fabs(fit.direction[1]), 2.0 * inv, 1e-12);
    CHECK(fit.direction[0] * fit.direction[1] > 0.0);
    CHECK_NEAR(fit.rms, 0.0, 1e-9);
    CHECK_NEAR(fit.centroid[0], 3.0, 1e-12);
    CHECK_NEAR(fit.centroid[1], 5.0, 1e-12);
    CHECK_NEAR(fit.extent, std::sqrt(2.0 * 5.0), 1e-9);  // sqrt of the variance along the line
    CHECK_EQ(fit.count, size_t(5));
    CHECK(!Math::fitLine2D(xs, ys, 1).ok);
    Math::LineFitAccumulator same;
    same.add(1.0, 1.0);
    same.add(1.0, 1.0);
    CHECK(!same.fit().ok);
    Math::LineFitAccumulator nan;
    nan.add(0.0, 0.0);
    nan.add(1.0, 1.0);
    nan.add(std::numeric_limits<double>::infinity(), 0.0);
    CHECK(!nan.fit().ok);
}

BREZEL_TEST(math_circle_fit, "Math/CircleFit.hpp",
            "the Kasa fit of points on a circle gives its centre and radius with RMS 0; collinear points or fewer "
            "than 3 give ok false") {
    std::vector<double> xs, ys;
    for (int i = 0; i < 8; ++i) {
        const double a = 0.3 + i * 0.7;
        xs.push_back(3.0 + 5.0 * std::cos(a));
        ys.push_back(-2.0 + 5.0 * std::sin(a));
    }
    auto fit = Math::fitCircleKasa(xs.data(), ys.data(), xs.size());
    REQUIRE(fit.ok);
    CHECK_NEAR(fit.centre[0], 3.0, 1e-9);
    CHECK_NEAR(fit.centre[1], -2.0, 1e-9);
    CHECK_NEAR(fit.radius, 5.0, 1e-9);
    CHECK_NEAR(fit.rms, 0.0, 1e-9);
    const double lx[] = {0.0, 1.0, 2.0, 3.0};
    const double ly[] = {0.0, 1.0, 2.0, 3.0};
    CHECK(!Math::fitCircleKasa(lx, ly, 4).ok);
    CHECK(!Math::fitCircleKasa(lx, ly, 2).ok);
}

// ---------------------------------------------------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(console_get_set_call_ls, "Console/Console.hpp",
            "the console reads, writes and calls by Entity.Component.property path through reflection; set writes "
            "the live field directly (also a ReadOnly one) and parses quotes; every malformed or unknown path is an "
            "[ERROR] line; ls lists components and children; completion lists verbs and paths") {
    ComponentRegistry::registerComponent<Switch>("Switch");
    registerTestComponents();
    TestWorld world("Console", false);
    Entity thing = world.project.createEntity("Thing");
    Entity child = world.project.createEntity("Kid");
    child.setParent(thing);
    thing.add<Position>(1.5f, 2.0f, 3);
    thing.add<Switch>();
    thing.add<Label>();
    Console console(world.project);
    std::vector<std::string> out;
    console.onOutput.connect([&](std::string line) { out.push_back(std::move(line)); });
    auto last = [&] { return out.empty() ? std::string() : out.back(); };

    console.exec("get Thing.Position.x");
    CHECK_EQ(last(), std::string("[OK] Thing.Position.x = 1.500000"));
    console.exec("set Thing.Position.layer 9");
    CHECK_EQ(thing.get<Position>().layer, 9);
    console.exec("set Thing.Switch.count 5");  // ReadOnly is not enforced
    CHECK_EQ(thing.get<Switch>().count, 5);
    console.exec("set Thing.Switch.on false");
    CHECK(!thing.get<Switch>().on);
    console.exec("set Thing.Label.text \"two words\"");
    CHECK_EQ(thing.get<Label>().text, std::string("two words"));
    console.exec("call Thing.Switch.reset");
    CHECK_EQ(thing.get<Switch>().count, 0);
    CHECK_EQ(last(), std::string("[OK] Executed Thing.Switch.reset"));

    console.exec("get Nobody.Position.x");
    CHECK_EQ(last(), std::string("[ERROR] Unknown entity 'Nobody'"));
    console.exec("get Thing.NoComp.x");
    CHECK(last().find("Component 'NoComp' not found") != std::string::npos);
    console.exec("get Thing.Position.zz");
    CHECK(last().find("Property or action 'zz' not found") != std::string::npos);
    console.exec("set Thing.Position.x abc");
    CHECK(last().find("[ERROR] Type conversion failed") != std::string::npos);
    console.exec("get Thing");
    CHECK(last().find("Invalid path format") != std::string::npos);
    console.exec("get");
    CHECK_EQ(last(), std::string("[ERROR] Missing path argument."));
    console.exec("frob Thing.Position.x");
    CHECK_EQ(last(), std::string("[ERROR] Unknown command verb 'frob'"));
    console.exec("call Thing.Position.x");
    CHECK(last().find("is not an action") != std::string::npos);
    console.exec("ls Thing");
    CHECK(last().find("  - Position") != std::string::npos);
    CHECK(last().find("Children:\n  - Kid") != std::string::npos);
    console.exec("undo");
    CHECK_EQ(last(), std::string("[OK] Undid last command."));

    CHECK((console.completeVerb("se") == std::vector<std::string>{"set"}));
    const auto paths = console.completePath("Th", "get");
    CHECK(std::find(paths.begin(), paths.end(), "Thing/") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "Thing.") != paths.end());
    const auto props = console.completePath("Thing.Position.l", "get");
    CHECK((props == std::vector<std::string>{"Thing.Position.layer"}));
    CHECK((console.parse_args("a \"b c\"  d") == std::vector<std::string>{"a", "b c", "d"}));
}

BREZEL_TEST(known_defect_B16_console_string_id_and_bool, "Console/SearchVisitor.hpp",
            "the console's SearchVisitor finds a StringID property with type None, so get reports it as not found; "
            "ConsoleDebugVisitor has no bool overload, so printEntity prints no bool property (09_BREZEL.md B-16)") {
    registerTestComponents();
    TestWorld world("Console", false);
    Entity thing = world.project.createEntity("Thing");
    thing.add<Label>(Label{"shown", true, StringID::from("kindvalue"), ""});
    Console console(world.project);
    std::string lastLine;
    console.onOutput.connect([&](std::string line) { lastLine = std::move(line); });
    console.exec("get Thing.Label.kind");
    CHECK_EQ(lastLine, std::string("[ERROR] Property or action 'kind' not found on component 'Label'"));
    console.exec("get Thing.Label.visible");
    CHECK_EQ(lastLine, std::string("[OK] Thing.Label.visible = true"));

    CoutCapture capture;
    printProject(world.project);
    const std::string printed = capture.buffer.str();
    CHECK(printed.find("[Entity] \"Thing\"") != std::string::npos);
    CHECK(printed.find("text: shown") != std::string::npos);
    CHECK(printed.find("kind: kindvalue") != std::string::npos);
    CHECK(printed.find("visible") == std::string::npos);
}

// ---------------------------------------------------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(application_projects, "App/Application.hpp",
            "createProject adds a project and makes it active; closeProject removes one and makes the first "
            "remaining project active; with none left there is no active project; saveProject writes the XML file") {
    Project* a = Application::createProject("A");
    Project* b = Application::createProject("B");
    CHECK(Application::getActiveProject() == b);
    Application::closeProject(1);
    CHECK(Application::getActiveProject() == a);
    a->setWriterThread(std::this_thread::get_id());
    a->createEntity("Saved");
    const auto path = std::filesystem::temp_directory_path() /
                      ("brezel_app_" + std::to_string(UUIDProvider::generate().value) + ".xml");
    CHECK(Application::saveProject(a, path.string()));
    CHECK(std::filesystem::exists(path));
    std::filesystem::remove(path);
    a->setWriterThread(std::thread::id{});
    Application::closeProject(0);
    CHECK(Application::getActiveProject() == nullptr);
    Application::closeProject(5);  // out of range: nothing
}

BREZEL_TEST(known_defect_app_load_without_lock, "App/Application.hpp",
            "Application::loadProject loads into a new project with no lock and no writer thread, so the ECS wall "
            "counts every entity access (and stops a build without NDEBUG unless BREZEL_ECS_WALL=count; "
            "Framework_Guide.md 6.1); it refuses a file with a Critical error and keeps one with warnings") {
    registerTestComponents();
    const auto dir = std::filesystem::temp_directory_path();
    const auto good = dir / ("brezel_app_good_" + std::to_string(UUIDProvider::generate().value) + ".xml");
    const auto critical = dir / ("brezel_app_bad_" + std::to_string(UUIDProvider::generate().value) + ".xml");
    {
        TestWorld source("Loaded Name", false);
        source.project.createEntityWithUUID(UUID(4001), "E").add<Position>(1.0f, 2.0f, 3);
        CHECK(Xml::saveProject(source.project, good.string()));
        tinyxml2::XMLDocument doc;
        auto* root = doc.NewElement("Project");
        doc.InsertEndChild(root);
        root->SetAttribute("Name", "Bad");
        auto* ent = doc.NewElement("Entity");  // no UUID: Critical
        ent->SetAttribute("Name", "NoUuid");
        ent->SetAttribute("DisplayName", "NoUuid");
        root->InsertEndChild(ent);
        CHECK(doc.SaveFile(critical.string().c_str()) == tinyxml2::XML_SUCCESS);
    }
    {
        CountModeEnvironment countMode;
        Project* loaded = Application::loadProject(good.string());
        REQUIRE(loaded != nullptr);
        CHECK(Application::getActiveProject() == loaded);
        CHECK_EQ(loaded->getName(), std::string_view("Loaded Name"));
        CHECK(loaded->ecsWall().totalViolations() > 0);  // the loader's entity accesses, outside any scope
        loaded->setWriterThread(std::this_thread::get_id());
        CHECK(loaded->getEntityByUUID(UUID(4001)).has<Position>());
        loaded->setWriterThread(std::thread::id{});
        CHECK(Application::loadProject(critical.string()) == nullptr);
        CHECK(Application::getActiveProject() == loaded);
    }
    Application::closeProject(0);
    std::filesystem::remove(good);
    std::filesystem::remove(critical);
}

BREZEL_TEST_MAIN("rest")
