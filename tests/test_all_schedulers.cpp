#include <iostream>
#include <vector>
#include <string>

#include "common/types.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/random_scheduler.hpp"
#include "scheduler/spread_scheduler.hpp"
#include "scheduler/openwhisk_scheduler.hpp"
#include "scheduler/openwhisk_warm_scheduler.hpp"
#include "scheduler/helper_scheduler.hpp"
#include "scheduler/pasch_scheduler.hpp"

using namespace kumo;

namespace {

bool expect(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        return false;
    }
    return true;
}

// Build a simple platform with 3 workers and 2 functions (two tenants).
PlatformState make_basic_platform() {
    PlatformState ps;

    // Two functions with different tenants.
    FunctionProfile fA;
    fA.id        = 1;
    fA.tenant    = 10;
    fA.owner     = 10;
    fA.name      = "funcA";
    fA.resources = { .cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 5 };

    FunctionProfile fB;
    fB.id        = 2;
    fB.tenant    = 20;
    fB.owner     = 20;
    fB.name      = "funcB";
    fB.resources = { .cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 5 };
    fB.packages  = {"pkgB"}; // used by PASch

    ps.add_function(fA);
    ps.add_function(fB);

    // Three identical workers.
    ResourceConfig cap{ .cpu_cores = 2.0, .memory_mb = 1024, .storage_mb = 50 };
    ps.add_worker(cap); // 0
    ps.add_worker(cap); // 1
    ps.add_worker(cap); // 2

    return ps;
}

// Helper: apply resource usage + tenant presence to the chosen worker.
void apply_allocation(PlatformState& ps,
                      WorkerId wid,
                      const Invocation& inv)
{
    auto* func = ps.get_function(inv.function_id());
    if (!func) return;

    auto& w = ps.mutable_worker(wid);
    w.used.cpu_cores  += func->resources.cpu_cores;
    w.used.memory_mb  += func->resources.memory_mb;
    w.used.storage_mb += func->resources.storage_mb;
    w.active_invocations += 1;

    if (std::find(w.tenants_present.begin(),
                  w.tenants_present.end(),
                  inv.tenant_id()) == w.tenants_present.end()) {
        w.tenants_present.push_back(inv.tenant_id());
    }
}

// Helper: reset usage and tenants (for clean tests).
void reset_usage(PlatformState& ps) {
    for (WorkerId wid : ps.workers()) {
        auto& w = ps.mutable_worker(wid);
        w.used = ResourceConfig{};
        w.active_invocations = 0;
        w.tenants_present.clear();
        // keep warm_functions as-is; some tests rely on it explicitly
    }
}

// ---------------- Tests ----------------

bool test_random_scheduler() {
    std::cout << "=== RandomScheduler ===\n";
    auto ps = make_basic_platform();
    RandomScheduler sched(123);

    Invocation inv(1, 1, 10, 10, 0.0, 10.0);

    std::vector<WorkerId> chosen;
    for (int i = 0; i < 10; ++i) {
        auto dec = sched.schedule(inv, ps);
        if (!expect(dec.success, "RandomScheduler failed with capacity available"))
            return false;
        chosen.push_back(dec.worker_id);
    }

    std::cout << "Workers chosen: ";
    for (auto w : chosen) std::cout << w << " ";
    std::cout << "\n";
    return true; // we mostly care it doesn't fail and IDs are in range
}

bool test_spread_scheduler() {
    std::cout << "\n=== SpreadScheduler ===\n";
    auto ps = make_basic_platform();
    SpreadScheduler sched(123);

    // Phase 1: tenant 10, function 1, multiple invocations
    Invocation invA(100, 1, 10, 10, 0.0, 10.0);

    WorkerId first_worker = 0;
    for (int i = 0; i < 3; ++i) {
        auto dec = sched.schedule(invA, ps);
        if (!expect(dec.success, "SpreadScheduler failed (tenant 10)"))
            return false;
        if (i == 0) {
            first_worker = dec.worker_id;
        } else {
            if (!expect(dec.worker_id == first_worker,
                        "SpreadScheduler did not keep same tenant on same worker"))
                return false;
        }
        apply_allocation(ps, dec.worker_id, invA);
    }

    std::cout << "Tenant 10 placed on worker " << first_worker << ".\n";

    // Phase 2: tenant 20, function 2, should prefer worker with no tenants.
    Invocation invB(200, 2, 20, 20, 0.0, 10.0);
    auto decB = sched.schedule(invB, ps);
    if (!expect(decB.success, "SpreadScheduler failed (tenant 20)"))
        return false;

    WorkerId wB = decB.worker_id;
    std::cout << "Tenant 20 placed on worker " << wB << ".\n";

    // Worker with tenant 10 has variety >=1; the other workers start at variety 0.
    // So we expect tenant 20 to go to some worker != first_worker.
    if (!expect(wB != first_worker,
                "SpreadScheduler placed new tenant on existing-tenant worker (expected fewer tenants)"))
        return false;

    return true;
}

bool test_openwhisk_scheduler() {
    std::cout << "\n=== OpenWhiskScheduler ===\n";
    auto ps = make_basic_platform();
    OpenWhiskScheduler sched(123);

    Invocation inv(1, 1, 10, 10, 0.0, 10.0);

    // Basic: with full capacity, must succeed.
    auto dec = sched.schedule(inv, ps);
    if (!expect(dec.success, "OpenWhiskScheduler failed unexpectedly"))
        return false;
    std::cout << "Initial placement on worker: " << dec.worker_id << "\n";

    // Now remove capacity from all workers => must fail.
    for (WorkerId wid : ps.workers()) {
        auto& w = ps.mutable_worker(wid);
        w.used.cpu_cores  = w.capacity.cpu_cores;
        w.used.memory_mb  = w.capacity.memory_mb;
        w.used.storage_mb = w.capacity.storage_mb;
    }

    auto dec2 = sched.schedule(inv, ps);
    if (!expect(!dec2.success, "OpenWhiskScheduler should fail when no capacity"))
        return false;

    return true;
}

bool test_openwhisk_warm_scheduler() {
    std::cout << "\n=== OpenWhiskWarmScheduler ===\n";
    auto ps = make_basic_platform();
    OpenWhiskWarmScheduler sched(123);

    Invocation inv(1, 1, 10, 10, 0.0, 10.0);

    // Mark worker 1 as having a warm container for func 1.
    {
        auto& w1 = ps.mutable_worker(1);
        w1.warm_functions.push_back(1);
    }

    auto dec = sched.schedule(inv, ps);
    if (!expect(dec.success, "OpenWhiskWarmScheduler failed unexpectedly"))
        return false;

    std::cout << "Chosen worker (should prefer warm=1): " << dec.worker_id << "\n";

    if (!expect(dec.worker_id == 1,
                "OpenWhiskWarmScheduler did not choose warm worker"))
        return false;

    return true;
}

bool test_helper_scheduler() {
    std::cout << "\n=== HelperScheduler ===\n";
    auto ps = make_basic_platform();
    // Use a small threshold to encourage scale-out quickly.
    HelperScheduler sched(123, /*threshold=*/2);

    Invocation inv(1, 1, 10, 10, 0.0, 10.0);

    std::vector<WorkerId> chosen;
    for (int i = 0; i < 6; ++i) {
        auto dec = sched.schedule(inv, ps);
        if (!expect(dec.success, "HelperScheduler failed unexpectedly"))
            return false;
        chosen.push_back(dec.worker_id);
        apply_allocation(ps, dec.worker_id, inv);
    }

    std::cout << "Worker sequence for func 1: ";
    for (auto w : chosen) std::cout << w << " ";
    std::cout << "\n";

    // Expect at least 2 distinct workers due to scale-out.
    std::sort(chosen.begin(), chosen.end());
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
    if (!expect(chosen.size() >= 2,
                "HelperScheduler did not scale out to multiple workers"))
        return false;

    return true;
}

bool test_pasch_scheduler() {
    std::cout << "\n=== PASchScheduler ===\n";
    auto ps = make_basic_platform();
    PASchScheduler sched(123);

    // Two different functions, likely mapped to different workers via packages.
    Invocation invA(1, 1, 10, 10, 0.0, 10.0);
    Invocation invB(2, 2, 20, 20, 0.0, 10.0);

    std::vector<WorkerId> seqA;
    for (int i = 0; i < 5; ++i) {
        auto dec = sched.schedule(invA, ps);
        if (!expect(dec.success, "PASchScheduler failed for funcA"))
            return false;
        seqA.push_back(dec.worker_id);
    }

    std::vector<WorkerId> seqB;
    for (int i = 0; i < 5; ++i) {
        auto dec = sched.schedule(invB, ps);
        if (!expect(dec.success, "PASchScheduler failed for funcB"))
            return false;
        seqB.push_back(dec.worker_id);
    }

    std::cout << "PASch workers for funcA: ";
    for (auto w : seqA) std::cout << w << " ";
    std::cout << "\n";
    std::cout << "PASch workers for funcB: ";
    for (auto w : seqB) std::cout << w << " ";
    std::cout << "\n";

    // For each function, we expect consistent mapping (same worker repeatedly)
    auto refA = seqA.front();
    bool allA = std::all_of(seqA.begin(), seqA.end(),
                            [refA](WorkerId w){ return w == refA; });
    if (!expect(allA, "PASchScheduler did not consistently map funcA"))
        return false;

    auto refB = seqB.front();
    bool allB = std::all_of(seqB.begin(), seqB.end(),
                            [refB](WorkerId w){ return w == refB; });
    if (!expect(allB, "PASchScheduler did not consistently map funcB"))
        return false;

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok &= test_random_scheduler();
    ok &= test_spread_scheduler();
    ok &= test_openwhisk_scheduler();
    ok &= test_openwhisk_warm_scheduler();
    ok &= test_helper_scheduler();
    ok &= test_pasch_scheduler();

    if (ok) {
        std::cout << "\nAll scheduler tests: [PASS]\n";
        return 0;
    } else {
        std::cerr << "\nSome scheduler tests: [FAIL]\n";
        return 1;
    }
}
