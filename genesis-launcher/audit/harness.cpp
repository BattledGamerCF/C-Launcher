// Genesis ProcessHandle runtime verification harness.
// Spawns real OS processes, exercises invariants, emits structured JSONL.
// No source-reasoning: every PASS/FAIL line is an observed runtime measurement.

#include "genesis/jvm/ProcessHandle.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace genesis::jvm;

// ─── tiny event log (monotonic ns, thread id, json-ish) ───────────────────
static std::mutex g_evt_mu;
static FILE*      g_evt = nullptr;
static int64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
static void evt(const char* kind, const std::string& payload) {
    std::lock_guard<std::mutex> l(g_evt_mu);
    if (!g_evt) return;
    fprintf(g_evt, "{\"t\":%lld,\"th\":%zu,\"k\":\"%s\",%s}\n",
            (long long)now_ns(),
            std::hash<std::thread::id>{}(std::this_thread::get_id()),
            kind, payload.c_str());
}

// ─── spawn helpers (fork/setsid/exec) ─────────────────────────────────────
struct Spawned { pid_t pid; pid_t pgid_observed; };

static Spawned spawn_setsid(const char* path, const char* arg) {
    pid_t pid = fork();
    if (pid == 0) {
        if (setsid() < 0) { _exit(127); }
        execl(path, path, arg, (char*)nullptr);
        _exit(127);
    }
    // give the child a moment to setsid
    for (int i = 0; i < 50; ++i) {
        pid_t pg = getpgid(pid);
        if (pg == pid) return {pid, pg};
        usleep(1000);
    }
    return {pid, getpgid(pid)};
}

// Spawn a 3-level chain via direct fork() (no shell), all in one session.
// Writes lvl0/1/2 pid files and sleeps. Returns leader pid.
static pid_t spawn_chain_leader(int /*depth*/, const char* dir) {
    pid_t leader = fork();
    if (leader != 0) return leader;
    if (setsid() < 0) _exit(127);
    char p0[256], p1[256], p2[256];
    snprintf(p0,sizeof(p0),"%s/lvl0.pid",dir);
    snprintf(p1,sizeof(p1),"%s/lvl1.pid",dir);
    snprintf(p2,sizeof(p2),"%s/lvl2.pid",dir);
    { FILE*f=fopen(p0,"w"); if(f){fprintf(f,"%d",getpid()); fclose(f);} }
    pid_t mid = fork();
    if (mid == 0) {
        { FILE*f=fopen(p1,"w"); if(f){fprintf(f,"%d",getpid()); fclose(f);} }
        pid_t leaf = fork();
        if (leaf == 0) {
            { FILE*f=fopen(p2,"w"); if(f){fprintf(f,"%d",getpid()); fclose(f);} }
            // sleep without execing /bin/sleep to avoid sandbox supervisor noise
            for (int i = 0; i < 600; ++i) { struct timespec ts{0, 100000000}; nanosleep(&ts, nullptr); }
            _exit(0);
        }
        for (int i = 0; i < 600; ++i) { struct timespec ts{0, 100000000}; nanosleep(&ts, nullptr); }
        _exit(0);
    }
    for (int i = 0; i < 600; ++i) { struct timespec ts{0, 100000000}; nanosleep(&ts, nullptr); }
    _exit(0);
}

// Adopt into a ProcessHandle (no os_handle on POSIX).
static std::unique_ptr<ProcessHandle> adopt(pid_t pid, std::string id) {
    auto h = std::make_unique<ProcessHandle>();
    h->adopt(pid, nullptr, std::move(id));
    return h;
}

// ─── INV-3: wait() one-time gate, ≥100 cycles × N concurrent waiters ──────
struct InvResult { int passed=0; int failed=0; std::vector<std::string> failures; };

static InvResult inv_wait_one_time_gate(int cycles, int waiters_per_cycle) {
    InvResult R;
    for (int c = 0; c < cycles; ++c) {
        // spawn /bin/true under setsid so killpg is valid
        pid_t pid = fork();
        if (pid == 0) { setsid(); execl("/bin/true","/bin/true",(char*)nullptr); _exit(127); }
        auto h = adopt(pid, "i" + std::to_string(c));

        std::atomic<int> got_ok{0}, got_err{0};
        std::vector<std::thread> ts;
        ts.reserve(waiters_per_cycle);
        for (int i = 0; i < waiters_per_cycle; ++i) {
            ts.emplace_back([&]{
                auto r = h->wait();
                if (r.is_ok()) got_ok++; else got_err++;
            });
        }
        for (auto& t : ts) t.join();

        if (got_ok == waiters_per_cycle && got_err == 0) {
            R.passed++;
        } else {
            R.failed++;
            R.failures.push_back("cycle="+std::to_string(c)+" ok="+std::to_string(got_ok)+
                                 " err="+std::to_string(got_err));
        }
        evt("inv_wait_gate_cycle", "\"c\":"+std::to_string(c)+
            ",\"ok\":"+std::to_string(got_ok)+",\"err\":"+std::to_string(got_err)+
            ",\"exit\":"+std::to_string(h->exit_code()));
    }
    return R;
}

// ─── INV-4: dispatch order: Running enqueued before terminal under fast exit
// publish_current_state must fire callback BEFORE wait() returns terminal.
static InvResult inv_dispatch_order_fast_exit(int cycles) {
    InvResult R;
    for (int c = 0; c < cycles; ++c) {
        pid_t pid = fork();
        if (pid == 0) { setsid(); _exit(0); }   // fastest possible exit
        auto h = adopt(pid, "i"+std::to_string(c));

        std::mutex m;
        std::vector<std::pair<int,int>> events; // (prev,next) sequence
        h->on_transition([&](ProcessState p, ProcessState n, int32_t){
            std::lock_guard<std::mutex> l(m);
            events.emplace_back((int)p, (int)n);
        });
        h->publish_current_state();          // must enqueue Running first
        auto r = h->wait();                  // must enqueue terminal second
        (void)r;

        // Required order: first event (Running,Running); last event ends in
        // terminal (Stopped or Crashed). At minimum >=2 events.
        bool ok = events.size() >= 2 &&
                  events.front().first  == (int)ProcessState::Running &&
                  events.front().second == (int)ProcessState::Running &&
                  (events.back().second == (int)ProcessState::Stopped ||
                   events.back().second == (int)ProcessState::Crashed);
        if (ok) R.passed++; else {
            R.failed++;
            std::string s = "cycle="+std::to_string(c)+" n="+std::to_string(events.size());
            for (auto& e : events) s += " ("+std::to_string(e.first)+"->"+std::to_string(e.second)+")";
            R.failures.push_back(s);
        }
    }
    return R;
}

// ─── INV-9: setsid sets pgid == pid (≥100 forks) ──────────────────────────
static InvResult inv_setsid_pgid(int cycles) {
    InvResult R;
    for (int c = 0; c < cycles; ++c) {
        auto s = spawn_setsid("/bin/sleep", "0.05");
        if (s.pgid_observed == s.pid) R.passed++;
        else { R.failed++;
               R.failures.push_back("cycle="+std::to_string(c)+
                                    " pid="+std::to_string(s.pid)+
                                    " pgid="+std::to_string(s.pgid_observed)); }
        // reap
        int st; waitpid(s.pid, &st, 0);
    }
    return R;
}

// ─── INV-8: killpg reaps a 3-level subprocess chain ───────────────────────
static bool pid_alive(pid_t p) { return ::kill(p, 0) == 0 || errno != ESRCH; }

static InvResult inv_killpg_chain(int cycles) {
    InvResult R;
    char dir_template[] = "/tmp/genesis_chainXXXXXX";
    for (int c = 0; c < cycles; ++c) {
        char dir[64]; strncpy(dir, dir_template, sizeof(dir));
        if (!mkdtemp(dir)) { R.failed++; R.failures.push_back("mkdtemp"); continue; }
        pid_t leader = spawn_chain_leader(3, dir);
        // wait until all 3 pid files exist
        pid_t lvl0=0, lvl1=0, lvl2=0;
        for (int i = 0; i < 200; ++i) {
            usleep(2000);
            FILE* f;
            if (!lvl0) { f=fopen((std::string(dir)+"/lvl0.pid").c_str(),"r"); if(f){fscanf(f,"%d",&lvl0); fclose(f);} }
            if (!lvl1) { f=fopen((std::string(dir)+"/lvl1.pid").c_str(),"r"); if(f){fscanf(f,"%d",&lvl1); fclose(f);} }
            if (!lvl2) { f=fopen((std::string(dir)+"/lvl2.pid").c_str(),"r"); if(f){fscanf(f,"%d",&lvl2); fclose(f);} }
            if (lvl0 && lvl1 && lvl2) break;
        }
        if (!(lvl0 && lvl1 && lvl2)) {
            R.failed++; R.failures.push_back("cycle="+std::to_string(c)+" missing pidfiles");
            ::killpg(leader, SIGKILL);
            int st; while (waitpid(-leader, &st, WNOHANG) > 0) {}
            continue;
        }
        auto h = adopt(leader, "chain"+std::to_string(c));
        auto kr = h->kill();
        // Reap the leader FIRST so kill(leader,0) doesn't return 0 due
        // to a zombie still in the process table. Descendants are
        // reparented to init and will be reaped there; we then probe
        // their PIDs for true ESRCH disappearance.
        int st; waitpid(leader, &st, 0);
        bool any_alive = true;
        for (int i = 0; i < 500 && any_alive; ++i) {
            usleep(2000);
            errno=0; bool a1 = (::kill(lvl1,0)==0) && errno != ESRCH;
            errno=0; bool a2 = (::kill(lvl2,0)==0) && errno != ESRCH;
            // lvl0 is leader; already reaped above
            any_alive = a1 || a2;
        }
        if (!any_alive && kr.is_ok()) R.passed++;
        else { R.failed++;
               R.failures.push_back("cycle="+std::to_string(c)+" killpg_ok="+
                                    std::to_string(kr.is_ok())+
                                    " any_alive="+std::to_string(any_alive)); }
        // cleanup pidfiles
        unlink((std::string(dir)+"/lvl0.pid").c_str());
        unlink((std::string(dir)+"/lvl1.pid").c_str());
        unlink((std::string(dir)+"/lvl2.pid").c_str());
        rmdir(dir);
    }
    return R;
}

// ─── INV-13: zombie-then-OS-reap → does Stopped overwrite Zombie? ─────────
// Simulate: mark_zombie, then wait() reaps. Capture transition sequence.
static InvResult inv_zombie_then_reap(int cycles) {
    InvResult R;
    for (int c = 0; c < cycles; ++c) {
        pid_t pid = fork();
        if (pid == 0) { setsid(); usleep(20000); _exit(0); }
        auto h = adopt(pid, "z"+std::to_string(c));
        std::mutex m; std::vector<int> seq;
        h->on_transition([&](ProcessState, ProcessState n, int32_t){
            std::lock_guard<std::mutex> l(m); seq.push_back((int)n);
        });
        h->mark_zombie("synthetic");
        // Now reap externally would normally occur; here wait() does it.
        auto r = h->wait();
        (void)r;
        // After mark_zombie + wait reap, sequence is observed.
        // Per spec: Zombie should be the terminal state if mark_zombie was called.
        // This test reveals whether wait() overwrites Zombie.
        bool zombie_present = false, terminal_after_zombie = false;
        for (size_t i = 0; i < seq.size(); ++i) {
            if (seq[i] == (int)ProcessState::Zombie) zombie_present = true;
            if (zombie_present && i+1 < seq.size()) {
                if (seq[i+1] == (int)ProcessState::Stopped ||
                    seq[i+1] == (int)ProcessState::Crashed) terminal_after_zombie = true;
            }
        }
        // Pass criterion: zombie is published exactly once and not overwritten.
        if (zombie_present && !terminal_after_zombie) R.passed++;
        else { R.failed++;
               std::string s = "cycle="+std::to_string(c)+" seq=";
               for (int v : seq) s += std::to_string(v)+",";
               R.failures.push_back(s); }
    }
    return R;
}

// ─── INV-10: PID-reuse safety probe ───────────────────────────────────────
// Spawn a process, wait/reap it via OS, then before adopting a new handle
// for the same (potentially) reused pid, check whether is_running() of the
// reaped handle returns false. This is a single-process probe; cannot
// guarantee PID reuse but covers post-reap state correctness.
static InvResult inv_post_reap_is_running(int cycles) {
    InvResult R;
    for (int c = 0; c < cycles; ++c) {
        pid_t pid = fork();
        if (pid == 0) { setsid(); _exit(0); }
        auto h = adopt(pid, "p"+std::to_string(c));
        auto r = h->wait();
        (void)r;
        bool running = h->is_running();
        if (!running) R.passed++;
        else { R.failed++;
               R.failures.push_back("cycle="+std::to_string(c)+" pid="+std::to_string(pid)+
                                    " running_after_reap=true"); }
    }
    return R;
}

// ─── runner ───────────────────────────────────────────────────────────────
static void print_inv(const char* name, const InvResult& r, int total) {
    int shown = 0;
    printf("INV %-40s passed=%d failed=%d  status=%s\n",
           name, r.passed, r.failed,
           (r.failed == 0 && r.passed == total) ? "VERIFIED PASS" : "VERIFIED FAIL");
    for (auto& f : r.failures) {
        if (shown++ >= 3) { printf("  ... %zu more\n", r.failures.size()-3); break; }
        printf("  FAIL: %s\n", f.c_str());
    }
    fflush(stdout);
}

int main(int argc, char** argv) {
    int N = (argc > 1) ? atoi(argv[1]) : 100;
    const char* only = (argc > 2) ? argv[2] : "all";
    g_evt = fopen("/tmp/genesis_audit/events.jsonl", "w");
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // Reap any orphaned children we may produce. Using default SIGCHLD here
    // because we explicitly wait() every spawn.

    printf("=== Genesis ProcessHandle runtime verification (N=%d cycles only=%s) ===\n", N, only);
    int total_fail = 0;
    auto run = [&](const char* tag, auto fn) {
        if (strcmp(only,"all")!=0 && strcmp(only,tag)!=0) return;
        auto r = fn();
        print_inv(tag, r, N);
        total_fail += r.failed;
    };

    run("wait_gate",      [&]{ return inv_wait_one_time_gate(N, 10); });
    run("dispatch_order", [&]{ return inv_dispatch_order_fast_exit(N); });
    run("setsid_pgid",    [&]{ return inv_setsid_pgid(N); });
    run("killpg_chain",   [&]{ return inv_killpg_chain(N); });
    run("zombie_reap",    [&]{ return inv_zombie_then_reap(N); });
    run("post_reap_run",  [&]{ return inv_post_reap_is_running(N); });

    if (g_evt) fclose(g_evt);
    return total_fail == 0 ? 0 : 1;
}
