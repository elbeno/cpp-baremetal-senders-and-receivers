#include "detail/common.hpp"

#include <async/connect.hpp>
#include <async/entail_continue_on.hpp>
#include <async/just_result_of.hpp>
#include <async/schedulers/trigger_manager.hpp>
#include <async/schedulers/trigger_scheduler.hpp>
#include <async/then.hpp>

#include <stdx/concepts.hpp>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
template <typename T>
constexpr auto type_string =
    stdx::ct_string<stdx::type_as_string<T>().size() + 1>{
        stdx::type_as_string<T>()};
} // namespace

TEMPLATE_TEST_CASE("entail_continue_on", "[entail_continue_on]",
                   decltype([] {})) {
    constexpr auto name = type_string<TestType>;
    int sent_value{42};
    int received_value{};

    auto s = async::just_result_of([&] { async::triggers<name>.run(); }) |
             async::entail_continue_on(async::trigger_scheduler<name>{}) |
             async::then([&] { received_value = sent_value; });
    auto op = async::connect(s, receiver{[] {}});
    async::start(op);
    CHECK(received_value == 42);
}

// TEST_CASE("entail_continue_on advertises what it sends", "[continue_on]") {
//     auto sched = test_scheduler<1>{};

//     auto n1 = async::just(42);
//     [[maybe_unused]] auto t = async::continue_on(n1, sched);
//     static_assert(async::sender_of<decltype(t), async::set_value_t(int)>);
// }
