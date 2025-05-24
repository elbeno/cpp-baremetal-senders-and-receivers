#include "detail/common.hpp"
#include "detail/debug_handler.hpp"

#include <async/concepts.hpp>
#include <async/connect.hpp>
#include <async/just.hpp>
#include <async/just_result_of.hpp>
#include <async/let_error.hpp>
#include <async/schedulers/inline_scheduler.hpp>
#include <async/schedulers/thread_scheduler.hpp>
#include <async/start_on.hpp>
#include <async/then.hpp>
#include <async/variant_sender.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

TEST_CASE("let_error", "[let_error]") {
    int value{};

    auto s = async::just_error(0);
    auto l = async::let_error(s, [](auto) { return async::just(42); });
    auto op = async::connect(l, receiver{[&](auto i) { value = i; }});
    async::start(op);
    CHECK(value == 42);
}

TEST_CASE("let_error error", "[let_error]") {
    int value{};

    auto s = async::just_error(0);
    auto l = async::let_error(s, [](auto) { return async::just_error(42); });
    auto op = async::connect(l, error_receiver{[&](auto i) { value = i; }});
    async::start(op);
    CHECK(value == 42);
}

TEST_CASE("let_error stopped", "[let_error]") {
    int value{};

    auto s = async::just_error(0);
    auto l = async::let_error(s, [](auto) { return async::just_stopped(); });
    auto op = async::connect(l, stopped_receiver{[&] { value = 42; }});
    async::start(op);
    CHECK(value == 42);
}

TEST_CASE("let_error advertises what it sends", "[let_error]") {
    auto s = async::just_error(false);
    [[maybe_unused]] auto l =
        async::let_error(s, [](auto) { return async::just(42); });
    STATIC_REQUIRE(async::sender_of<decltype(l), async::set_value_t(int)>);
}

TEST_CASE("let_error advertises errors", "[let_error]") {
    auto s = async::just_error(false);
    [[maybe_unused]] auto l =
        async::let_error(s, [](auto) { return async::just_error(42); });
    STATIC_REQUIRE(async::sender_of<decltype(l), async::set_error_t(int)>);
}

TEST_CASE("let_error advertises stopped", "[let_error]") {
    auto s = async::just_error(false);
    [[maybe_unused]] auto l =
        async::let_error(s, [](auto) { return async::just_stopped(); });
    STATIC_REQUIRE(async::sender_of<decltype(l), async::set_stopped_t()>);
}

TEST_CASE("let_error is pipeable", "[let_error]") {
    int value{};

    auto l = async::just_error(0) |
             async::let_error([](auto) { return async::just(42); });
    auto op = async::connect(l, receiver{[&](auto i) { value = i; }});
    async::start(op);
    CHECK(value == 42);
}

TEST_CASE("let_error is adaptor-pipeable", "[let_error]") {
    int value{};

    auto l = async::let_error([](int) { return async::just_error(42); }) |
             async::let_error([](int i) { return async::just(i * 2); });
    auto op = async::connect(async::just_error(0) | l,
                             receiver{[&](auto i) { value = i; }});
    async::start(op);
    CHECK(value == 84);
}

TEST_CASE("move-only value", "[let_error]") {
    int value{};

    auto s = async::just_error(0);
    auto l =
        async::let_error(s, [](auto) { return async::just(move_only{42}); });
    STATIC_REQUIRE(async::singleshot_sender<decltype(l), universal_receiver>);
    auto op = async::connect(std::move(l),
                             receiver{[&](auto &&mo) { value = mo.value; }});
    async::start(op);
    CHECK(value == 42);
}

TEST_CASE("let_error with variant", "[let_error]") {
    int value{};

    auto s = async::just_error(std::cref(value)) | async::let_error([](auto i) {
                 return async::make_variant_sender(
                     i.get() % 2 == 0, [=] { return async::just(i.get() / 2); },
                     [=] { return async::just(i.get() * 3 + 1); });
             });

    STATIC_REQUIRE(
        std::is_same_v<async::completion_signatures_of_t<decltype(s)>,
                       async::completion_signatures<async::set_value_t(int)>>);

    auto r = receiver{[&](auto i) { value = i; }};
    value = 6;
    {
        auto o = async::connect(s, r);
        async::start(o);
    }
    CHECK(value == 3);
    {
        auto o = async::connect(s, r);
        async::start(o);
    }
    CHECK(value == 10);
    {
        auto o = async::connect(s, r);
        async::start(o);
    }
    CHECK(value == 5);
    {
        auto o = async::connect(s, r);
        async::start(o);
    }
    CHECK(value == 16);
    {
        auto o = async::connect(s, r);
        async::start(o);
    }
    CHECK(value == 8);
    {
        auto o = async::connect(s, r);
        async::start(o);
    }
    CHECK(value == 4);
    {
        auto o = async::connect(s, r);
        async::start(o);
    }
    CHECK(value == 2);
    {
        auto o = async::connect(s, r);
        async::start(o);
    }
    CHECK(value == 1);
}

TEST_CASE("let_error propagates value", "[let_error]") {
    bool let_called{};
    int value{};

    auto s = async::just(42) | async::let_error([&] {
                 let_called = true;
                 return async::just();
             });
    auto op = async::connect(s, receiver{[&](auto i) { value = i; }});
    async::start(op);
    CHECK(value == 42);
    CHECK(not let_called);
}

TEST_CASE("let_error propagates stopped", "[let_error]") {
    bool let_called{};
    int value{};

    auto s = async::just_stopped() | async::let_error([&] {
                 let_called = true;
                 return async::just();
             });
    auto op = async::connect(s, stopped_receiver{[&] { value = 42; }});
    async::start(op);
    CHECK(value == 42);
    CHECK(not let_called);
}

TEST_CASE("let_error advertises pass-through completions", "[let_error]") {
    [[maybe_unused]] auto l = async::just(42) | async::let_error([](auto) {});
    STATIC_REQUIRE(async::sender_of<decltype(l), async::set_value_t(int)>);
}

TEST_CASE("let_error can be single shot", "[let_error]") {
    [[maybe_unused]] auto l =
        async::just_error(42) |
        async::let_error([](auto i) { return async::just(move_only{i}); });
    STATIC_REQUIRE(async::singleshot_sender<decltype(l)>);
}

TEST_CASE("let_error can be single shot with passthrough", "[let_error]") {
    [[maybe_unused]] auto l =
        async::just(move_only{42}) | async::let_error([](auto) { return 42; });
    STATIC_REQUIRE(async::singleshot_sender<decltype(l)>);
}

TEST_CASE("let_error stores result of input sender", "[let_error]") {
    int value{};
    auto s = async::just_error_result_of([] { return 42; }) |
             async::let_error([](int &v) { return async::just(&v); });

    auto op = async::connect(s, receiver{[&](int const *i) { value = *i; }});
    async::start(op);
    CHECK(value == 42);
}

TEST_CASE("let_error op state may complete synchronously", "[let_error]") {
    auto s = async::just_error(42) |
             async::let_error([](auto) { return async::just(); });
    [[maybe_unused]] auto op = async::connect(s, receiver{[] {}});
    STATIC_REQUIRE(async::synchronous<decltype(op)>);
}

TEST_CASE(
    "let_error op state may not complete synchronously if antecedent does not",
    "[let_error]") {
    auto const s =
        async::start_on(async::thread_scheduler{}, async::just_error(42)) |
        async::let_error([](auto) { return async::just(); });
    [[maybe_unused]] auto op = async::connect(s, receiver{[] {}});
    STATIC_REQUIRE(not async::synchronous<decltype(op)>);
}

TEST_CASE(
    "let_error op state may not complete synchronously if subsequent does not",
    "[let_error]") {
    auto const s = async::just_error(42) | async::let_error([](auto) {
                       return async::thread_scheduler{}.schedule();
                   });
    [[maybe_unused]] auto op = async::connect(s, receiver{[] {}});
    STATIC_REQUIRE(not async::synchronous<decltype(op)>);
}

template <>
inline auto async::injected_debug_handler<> =
    debug_handler<async::let_t<async::set_error_t>>{};

TEST_CASE("let_error can be debugged with a string", "[let_error]") {
    using namespace std::string_literals;
    debug_events.clear();

    auto s = async::just_error(42) |
             async::let_error([](int i) { return async::just_error(i); });
    auto op = async::connect(
        s, with_env{universal_receiver{},
                    async::prop{async::get_debug_interface_t{},
                                async::debug::named_interface<"op">{}}});
    async::start(op);
    CHECK(debug_events ==
          std::vector{"op let_error start"s, "op let_error set_error"s});
}

TEST_CASE("let_error can be named and debugged with a string", "[let_error]") {
    using namespace std::string_literals;
    debug_events.clear();

    auto s =
        async::just_error(42) | async::let_error<"let_error_name">(
                                    [](int i) { return async::just_error(i); });
    auto op = async::connect(
        s, with_env{universal_receiver{},
                    async::prop{async::get_debug_interface_t{},
                                async::debug::named_interface<"op">{}}});
    async::start(op);
    CHECK(debug_events == std::vector{"op let_error_name start"s,
                                      "op let_error_name set_error"s});
}

TEST_CASE("let_error produces debug signal on non-handled channel",
          "[let_error]") {
    using namespace std::string_literals;
    debug_events.clear();

    auto s = async::just_stopped() |
             async::let_error([] { return async::just_error(42); });
    auto op = async::connect(
        s, with_env{universal_receiver{},
                    async::prop{async::get_debug_interface_t{},
                                async::debug::named_interface<"op">{}}});
    async::start(op);
    CHECK(debug_events ==
          std::vector{"op let_error start"s, "op let_error set_stopped"s});
}
