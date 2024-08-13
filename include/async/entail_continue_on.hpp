#pragma once

#include <async/completes_synchronously.hpp>
#include <async/compose.hpp>
#include <async/concepts.hpp>
#include <async/get_completion_signatures.hpp>

#include <stdx/concepts.hpp>
#include <stdx/functional.hpp>

#include <type_traits>
#include <utility>

namespace async {
namespace _entail_continue_on {
template <typename Sched>
using scheduler_sender = decltype(std::declval<Sched>().schedule());

template <typename Sndr, typename Sched, typename Rcvr>
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct op_state {
    using first_rcvr = detail::universal_receiver<>;

    template <stdx::same_as_unqualified<Sndr> S, typename Sch, typename R>
    constexpr op_state(S &&s, Sch &&sch, R &&r)
        : first_ops{connect(std::forward<S>(s), first_rcvr{})},
          second_ops{
              connect(std::forward<Sch>(sch).schedule(), std::forward<R>(r))} {}
    constexpr op_state(op_state &&) = delete;

    using first_ops_t = connect_result_t<Sndr, first_rcvr>;
    using second_ops_t = connect_result_t<scheduler_sender<Sched>, Rcvr>;

    constexpr auto start() & -> void {
        static_assert(synchronous<first_ops_t>,
                      "The sender passed to entail_continue_on must complete "
                      "synchronously");
        static_assert(not synchronous<second_ops_t>,
                      "The scheduler passed to entail_continue_on must produce "
                      "a sender that completes asynchronously");
        async::start(second_ops);
        async::start(first_ops);
    }

    first_ops_t first_ops;
    second_ops_t second_ops;
};

template <typename Sndr, typename Sched> struct sender {
    using is_sender = void;

    [[no_unique_address]] Sndr sndr;
    [[no_unique_address]] Sched sched;

  public:
    template <async::receiver R>
    [[nodiscard]] constexpr auto
    connect(R &&r) && -> op_state<Sndr, Sched, std::remove_cvref_t<R>> {
        check_connect<sender &&, R>();
        static_assert(std::same_as<value_signatures_of_t<Sndr, env_of_t<R>>,
                                   completion_signatures<set_value_t()>>,
                      "The sender passed to entail_continue_on must send void");
        return {std::move(sndr), std::move(sched), std::forward<R>(r)};
    }

    template <async::receiver R>
        requires multishot_sender<Sndr> and std::copy_constructible<Sndr> and
                     std::copy_constructible<Sched>
    [[nodiscard]] constexpr auto
    connect(R &&r) const & -> op_state<Sndr, Sched, std::remove_cvref_t<R>> {
        check_connect<sender, R>();
        static_assert(std::same_as<value_signatures_of_t<Sndr, env_of_t<R>>,
                                   completion_signatures<set_value_t()>>,
                      "The sender passed to entail_continue_on must send void");
        return {sndr, sched, std::forward<R>(r)};
    }

    template <typename Env>
    [[nodiscard]] constexpr static auto get_completion_signatures(Env const &)
        -> completion_signatures_of_t<scheduler_sender<Sched>, Env> {
        return {};
    }
};

template <typename Sched> struct pipeable {
    [[no_unique_address]] Sched sched;

  private:
    template <async::sender S, stdx::same_as_unqualified<pipeable> Self>
    friend constexpr auto operator|(S &&s, Self &&self) -> async::sender auto {
        return sender<std::remove_cvref_t<S>, Sched>{
            std::forward<S>(s), std::forward<Self>(self).sched};
    }
};
} // namespace _entail_continue_on

template <typename Sched>
[[nodiscard]] constexpr auto entail_continue_on(Sched &&sched) {
    return _compose::adaptor{
        stdx::tuple{_entail_continue_on::pipeable<std::remove_cvref_t<Sched>>{
            std::forward<Sched>(sched)}}};
}

template <sender S, typename Sched>
[[nodiscard]] constexpr auto entail_continue_on(S &&s, Sched &&sched) -> sender
    auto {
    return std::forward<S>(s) | entail_continue_on(std::forward<Sched>(sched));
}
} // namespace async
