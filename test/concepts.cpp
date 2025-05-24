#include <async/completion_tags.hpp>
#include <async/concepts.hpp>
#include <async/connect.hpp>

#include <stdx/concepts.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("queryable", "[concepts]") {
    STATIC_REQUIRE(not async::queryable<void>);
    STATIC_REQUIRE(async::queryable<int>);
}

namespace {
struct op_state {
    auto start() & noexcept {}
};
struct not_op_state {};
} // namespace

TEST_CASE("operation_state", "[concepts]") {
    STATIC_REQUIRE(not async::operation_state<not_op_state>);
    STATIC_REQUIRE(async::operation_state<op_state>);
}

namespace {
struct receiver1 : async::receiver_base {};
struct receiver2 {
    using is_receiver = void;
};
struct not_receiver {};
} // namespace

TEST_CASE("receiver", "[concepts]") {
    STATIC_REQUIRE(not async::receiver<not_receiver>);
    STATIC_REQUIRE(async::receiver<receiver1>);
    STATIC_REQUIRE(async::receiver<receiver2>);
}

namespace {
struct error {};

template <typename E = error, typename... Ts>
struct receiver : async::receiver_base {
    constexpr auto set_value(std::same_as<Ts> auto...) const && -> void {}
    constexpr auto set_error(std::same_as<E> auto) const && -> void {}
    constexpr auto set_stopped() const && -> void {}
};
} // namespace

TEST_CASE("receiver_of", "[concepts]") {
    STATIC_REQUIRE(
        async::receiver_of<receiver<>,
                           async::completion_signatures<async::set_value_t()>>);
    STATIC_REQUIRE(async::receiver_of<
                   receiver<>,
                   async::completion_signatures<async::set_error_t(error)>>);
    STATIC_REQUIRE(
        async::receiver_of<
            receiver<>, async::completion_signatures<async::set_stopped_t()>>);

    STATIC_REQUIRE(async::receiver_of<
                   receiver<error, int>,
                   async::completion_signatures<async::set_value_t(int)>>);
    STATIC_REQUIRE(
        not async::receiver_of<
            receiver<>, async::completion_signatures<async::set_value_t(int)>>);
}

namespace {
struct typed_sender1 : async::sender_base {
    using completion_signatures = async::completion_signatures<>;
};
struct typed_sender2 {
    using is_sender = void;
    using completion_signatures = async::completion_signatures<>;
};
struct not_sender {};
} // namespace

TEST_CASE("sender", "[concepts]") {
    STATIC_REQUIRE(not async::sender<not_sender>);
    STATIC_REQUIRE(async::sender<typed_sender1>);
    STATIC_REQUIRE(async::sender<typed_sender2>);
}

namespace {
struct queryable_sender1 : async::sender_base {
    [[nodiscard]] constexpr auto get_completion_signatures(auto &&) noexcept
        -> async::completion_signatures<> {
        return {};
    }
};

struct dependent_env {};

struct queryable_sender2 : async::sender_base {
    [[nodiscard, maybe_unused]] constexpr auto
    get_completion_signatures(dependent_env const &) noexcept
        -> async::completion_signatures<> {
        return {};
    }
};
} // namespace

TEST_CASE("sender_in", "[concepts]") {
    STATIC_REQUIRE(not async::sender_in<not_sender>);
    STATIC_REQUIRE(async::sender_in<queryable_sender1>);
    STATIC_REQUIRE(not async::sender_in<queryable_sender2>);
    STATIC_REQUIRE(async::sender_in<queryable_sender2, dependent_env>);
}

namespace {
template <typename E = error, typename... Ts>
struct sender : async::sender_base {
    using completion_signatures =
        async::completion_signatures<async::set_value_t(Ts...),
                                     async::set_error_t(E)>;

    template <async::receiver_from<sender> R>
    constexpr auto connect(R &&) -> op_state {
        return {};
    }
};

template <typename... Ts> struct value_receiver : async::receiver_base {
    constexpr auto set_value(Ts...) const && -> void {}
    constexpr auto set_error(auto) const && -> void {}
};
} // namespace

TEST_CASE("sender_to", "[concepts]") {
    STATIC_REQUIRE(async::sender_to<sender<>, receiver<>>);
    STATIC_REQUIRE(async::sender_to<sender<error, int>, receiver<error, int>>);
    STATIC_REQUIRE(
        not async::sender_to<sender<error, int>, receiver<error, float>>);
}

TEST_CASE("sender_to value categories", "[concepts]") {
    STATIC_REQUIRE(
        async::sender_to<sender<error, int>, value_receiver<int const &>>);
    STATIC_REQUIRE(
        async::sender_to<sender<error, int>, value_receiver<int &&>>);
    STATIC_REQUIRE(
        not async::sender_to<sender<error, int>, value_receiver<int &>>);

    STATIC_REQUIRE(async::sender_to<sender<error, int &>, value_receiver<int>>);
    STATIC_REQUIRE(not async::sender_to<sender<error, int const &>,
                                        value_receiver<int &>>);
}

TEST_CASE("sender_of", "[concepts]") {
    STATIC_REQUIRE(
        async::sender_of<sender<error, int>, async::set_value_t(int)>);
    STATIC_REQUIRE(async::sender_of<sender<>, async::set_error_t(error)>);
    STATIC_REQUIRE(not async::sender_of<sender<>, async::set_value_t(int)>);
}

namespace {
template <typename E = error, typename... Ts>
struct singleshot_sender : async::sender_base {
    using completion_signatures =
        async::completion_signatures<async::set_value_t(Ts...),
                                     async::set_error_t(E)>;

    template <async::receiver_from<singleshot_sender> R>
    constexpr auto connect(R &&) && -> op_state {
        return {};
    }
};
} // namespace

TEST_CASE("single/multishot_sender", "[concepts]") {
    STATIC_REQUIRE(async::multishot_sender<sender<>, receiver<>>);
    STATIC_REQUIRE(async::singleshot_sender<singleshot_sender<>, receiver<>>);
}

namespace {
struct stoppable_sender : async::sender_base {
    using completion_signatures =
        async::completion_signatures<async::set_value_t(),
                                     async::set_stopped_t()>;

    template <async::receiver_from<stoppable_sender> R>
    constexpr auto connect(R &&) -> op_state {
        return {};
    }
};

struct dependent_stoppable_sender : async::sender_base {
    template <async::receiver_from<dependent_stoppable_sender> R>
    constexpr auto connect(R &&) -> op_state {
        return {};
    }

    template <typename Env>
    [[nodiscard]] constexpr static auto get_completion_signatures(Env const &)
        -> async::completion_signatures<async::set_value_t(),
                                        async::set_stopped_t()> {
        return {};
    }

    template <typename Env>
        requires async::unstoppable_token<async::stop_token_of_t<Env>>
    [[nodiscard]] constexpr static auto get_completion_signatures(Env const &)
        -> async::completion_signatures<async::set_value_t()> {
        return {};
    }
};

struct stoppable_env {
    async::inplace_stop_token stop_token;

    [[nodiscard, maybe_unused]] constexpr auto
    query(async::get_stop_token_t) const -> async::inplace_stop_token {
        return stop_token;
    }
};
} // namespace

TEST_CASE("stoppable sender", "[stop_token]") {
    STATIC_REQUIRE(not async::stoppable_sender<singleshot_sender<error, int>>);
    STATIC_REQUIRE(async::stoppable_sender<stoppable_sender>);
    STATIC_REQUIRE(not async::stoppable_sender<dependent_stoppable_sender>);
    STATIC_REQUIRE(
        async::stoppable_sender<dependent_stoppable_sender, stoppable_env>);
}
