#include "detail/common.hpp"

#include <async/concepts.hpp>
#include <async/connect.hpp>
#include <async/just.hpp>
#include <async/schedulers/inline_scheduler.hpp>
#include <async/schedulers/thread_scheduler.hpp>
#include <async/split.hpp>
#include <async/start_on.hpp>
#include <async/then.hpp>

#include <stdx/concepts.hpp>

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <utility>

TEST_CASE("split", "[split]") {
    bool recvd1{};
    bool recvd2{};
    auto s = async::inline_scheduler<>::schedule<
        async::inline_scheduler<>::singleshot>();
    STATIC_REQUIRE(async::singleshot_sender<decltype(s), universal_receiver>);
    auto spl = async::split(std::move(s));
    STATIC_REQUIRE(async::multishot_sender<decltype(spl), universal_receiver>);

    auto op1 = async::connect(spl, receiver{[&] { recvd1 = true; }});
    auto op2 = async::connect(spl, receiver{[&] { recvd2 = true; }});
    async::start(op1);
    CHECK(recvd1);

    CHECK(not recvd2);
    async::start(op2);
    CHECK(recvd2);
}

TEST_CASE("split error", "[split]") {
    bool recvd1{};
    bool recvd2{};
    auto s = async::just_error(move_only{42});
    STATIC_REQUIRE(async::singleshot_sender<decltype(s), universal_receiver>);
    auto spl = async::split(std::move(s));
    STATIC_REQUIRE(async::multishot_sender<decltype(spl), universal_receiver>);

    auto op1 =
        async::connect(spl, error_receiver{[&](auto &&) { recvd1 = true; }});
    auto op2 =
        async::connect(spl, error_receiver{[&](auto &&) { recvd2 = true; }});
    async::start(op1);
    CHECK(recvd1);

    CHECK(not recvd2);
    async::start(op2);
    CHECK(recvd2);
}

TEST_CASE("split advertises what it sends", "[split]") {
    auto s = async::just(move_only{42});
    auto spl = async::split(std::move(s));
    STATIC_REQUIRE(
        async::sender_of<decltype(spl), async::set_value_t(move_only<int>)>);
}

TEST_CASE("split advertises errors", "[split]") {
    auto s = async::just_error(move_only{42});
    auto spl = async::split(std::move(s));
    STATIC_REQUIRE(
        async::sender_of<decltype(spl), async::set_error_t(move_only<int>)>);
}

namespace {
auto test_split(int &value, int expected) {
    auto spl = async::inline_scheduler<>::schedule<
                   async::inline_scheduler<>::singleshot>() |
               async::then([&] { return expected; }) | async::split();
    STATIC_REQUIRE(async::multishot_sender<decltype(spl), universal_receiver>);

    bool run{};
    auto op = async::connect(spl, receiver{[&](int i) {
                                 run = true;
                                 value = i;
                             }});
    async::start(op);
    CHECK(run);
}
} // namespace

TEST_CASE("split called multiple times from same location", "[split]") {
    int value1{};
    test_split(value1, 42);
    CHECK(value1 == 42);

    int value2{};
    test_split(value2, 17);
    CHECK(value2 == 17);
}

TEST_CASE("split does nothing for multishot sender", "[split]") {
    auto s1 = async::inline_scheduler<>::schedule<
        async::inline_scheduler<>::multishot>();
    [[maybe_unused]] auto spl1 = std::move(s1) | async::split();
    STATIC_REQUIRE(std::same_as<decltype(s1), decltype(spl1)>);

    auto s2 = async::inline_scheduler<>::schedule<
        async::inline_scheduler<>::multishot>();
    [[maybe_unused]] auto spl2 = async::split(std::move(s2));
    STATIC_REQUIRE(std::same_as<decltype(s2), decltype(spl2)>);
}

TEST_CASE("split cancellation (stopped by source)", "[split]") {
    int stopped{};
    auto s = async::inline_scheduler<>::schedule<
        async::inline_scheduler<>::singleshot>();
    STATIC_REQUIRE(async::singleshot_sender<decltype(s), universal_receiver>);
    auto spl = async::split(std::move(s));

    auto r1 = stoppable_receiver{[&] { ++stopped; }};
    auto r2 = stopped_receiver{[&] { ++stopped; }};
    auto op1 = async::connect(spl, r1);
    auto op2 = async::connect(spl, r2);
    r1.request_stop();
    async::start(op1);
    CHECK(stopped == 1);

    async::start(op2);
    CHECK(stopped == 2);
}

TEST_CASE("split cancellation (stopped by sender)", "[split]") {
    int stopped{};
    auto s = async::start_on(singleshot_scheduler{}, async::just_stopped());
    STATIC_REQUIRE(async::singleshot_sender<decltype(s), universal_receiver>);
    auto spl = async::split(std::move(s));

    auto op1 = async::connect(spl, stopped_receiver{[&] { ++stopped; }});
    auto op2 = async::connect(spl, stopped_receiver{[&] { ++stopped; }});
    async::start(op1);
    CHECK(stopped == 1);

    async::start(op2);
    CHECK(stopped == 2);
}

namespace {
struct test_sender {
    using is_sender = void;
    using completion_signatures =
        async::completion_signatures<async::set_value_t()>;

    [[nodiscard]] constexpr static auto query(async::get_env_t) noexcept
        -> custom_env {
        return {};
    }

    template <typename R> struct op_state {
        R r;

        constexpr auto start() & -> void { async::set_value(std::move(r)); }
    };

    template <typename R>
    [[nodiscard]] constexpr auto
    connect(R &&r) && -> op_state<std::remove_cvref_t<R>> {
        return {std::forward<R>(r)};
    }
};
} // namespace

TEST_CASE("split sender environment", "[split]") {
    STATIC_REQUIRE(async::singleshot_sender<test_sender, universal_receiver>);
    auto s = test_sender{};
    CHECK(get_fwd(async::get_env(s)) == 42);

    auto spl = async::split(std::move(s));
    CHECK(get_fwd(async::get_env(spl)) == 42);
}

TEST_CASE("split may complete synchronously", "[split]") {
    auto s = async::inline_scheduler<>::schedule<
        async::inline_scheduler<>::singleshot>();
    [[maybe_unused]] auto spl = async::split(std::move(s));
    STATIC_REQUIRE(async::synchronous<decltype(s)>);
}

TEST_CASE("split may not complete synchronously", "[split]") {
    auto s = async::thread_scheduler<>::schedule();
    [[maybe_unused]] auto spl = async::split(std::move(s));
    STATIC_REQUIRE(not async::synchronous<decltype(s)>);
}

TEST_CASE("split op state may be synchronous", "[split]") {
    auto s = async::inline_scheduler<>::schedule<
        async::inline_scheduler<>::singleshot>();
    auto spl = async::split(std::move(s));
    [[maybe_unused]] auto op = async::connect(spl, receiver{[] {}});
    STATIC_REQUIRE(async::synchronous<decltype(op)>);
}

TEST_CASE("split op state may not be synchronous", "[split]") {
    auto s = async::thread_scheduler<>::schedule();
    auto spl = async::split(std::move(s));
    [[maybe_unused]] auto op = async::connect(spl, receiver{[] {}});
    STATIC_REQUIRE(not async::synchronous<decltype(op)>);
}
