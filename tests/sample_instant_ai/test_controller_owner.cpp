#include "../../src/sampler/controller_owner.hpp"

#include <cassert>

namespace {
struct FakeController {
    explicit FakeController(int& disposals) : disposals(disposals) {}
    void Dispose() { ++disposals; }
    int& disposals;
};
}

void test_controller_owner() {
    int disposals = 0;
    FakeController fake(disposals);
    {
        Sampler::ControllerOwner<FakeController> owner(&fake);
        owner.reset();
        owner.reset();
        assert(disposals == 1);
    }
    assert(disposals == 1);
}
