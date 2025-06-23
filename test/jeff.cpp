// experimenting with new repeat feature
#include <async/continue_on.hpp>
#include <async/incite_on.hpp>
#include <async/into_variant.hpp>
#include <async/let_value.hpp>
#include <async/repeat.hpp>
#include <async/schedulers/trigger_scheduler.hpp>
#include <async/sequence.hpp>
#include <async/start_detached.hpp>
#include <async/sync_wait.hpp>
#include <async/then.hpp>
#include <async/variant_sender.hpp>
#include <async/when_all.hpp>
#include <async/when_any.hpp>

#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <variant>

struct A {};
struct B {};

int msg_counter = 0;

TEST_CASE("jeff", "[jeff]") {
    using namespace async;

    std::variant<std::monostate, A, B> prev_msg;

    auto loop_fn = [&] {
        std::cerr << "running parent loop function\n";
        std::visit(
            [&](auto m) {
                std::cerr << "parent visiting " << typeid(decltype(m)).name()
                          << "\n";
                if constexpr (std::is_same_v<decltype(m), std::monostate>) {
                    // send initial message to other side
                    std::cerr << "parent sending A\n";
                    run_triggers<"thread">(A{});
                } else if (std::is_same_v<decltype(m), A>) {
                    // send opposite type
                    std::cerr << "parent sending B\n";
                    run_triggers<"thread">(B{});
                } else {
                    std::cerr << "parent sending A\n";
                    run_triggers<"thread">(A{});
                }
            },
            prev_msg);
        std::cerr << "parent waiting for A or B\n";
    };

    auto loop_body = when_any(trigger_scheduler<"main", A>{}.schedule(),
                              trigger_scheduler<"main", B>{}.schedule()) |
                     then([&](auto msg) {
                         ++msg_counter;
                         prev_msg = msg;
                     });

    auto predicate = [&] { return msg_counter > 5; };

    auto sndr = loop_body | repeat_until(predicate, loop_fn);

    // the same thing but a child thread
    std::thread t([] {
        std::cerr << "child thread running\n";
        using namespace std::literals;
        std::this_thread::sleep_for(1s); // yeah, it's a hack

        using namespace async;

        std::variant<std::monostate, A, B> prev_msg;

        auto loop_fn = [&] {
            std::cerr << "running child loop function\n";
            std::visit(
                [&](auto m) {
                    std::cerr << "child visiting " << typeid(decltype(m)).name()
                              << "\n";
                    if constexpr (std::is_same_v<decltype(m), std::monostate>) {
                        // send initial message to other side
                        std::cerr << "child sending A\n";
                        run_triggers<"main">(A{});
                    } else if (std::is_same_v<decltype(m), A>) {
                        // send opposite type
                        std::cerr << "child sending B\n";
                        run_triggers<"main">(B{});
                    } else {
                        std::cerr << "child sending A\n";
                        run_triggers<"main">(A{});
                    }
                },
                prev_msg);
            std::cerr << "child waiting for A or B\n";
        };

        auto loop_body = when_any(trigger_scheduler<"thread", A>{}.schedule(),
                                  trigger_scheduler<"thread", B>{}.schedule()) |
                         then([&](auto msg) { prev_msg = msg; });

        auto predicate = [&] { return msg_counter > 5; };

        auto sndr = loop_body | repeat_until(predicate, loop_fn);

        loop_fn();

        if (!sync_wait(sndr)) {
            std::cerr << "child thread sync_wait failed\n";
            exit(2);
        }
    });

    // send initial messages
    loop_fn();

    if (!sync_wait(sndr)) {
        std::cerr << "sync_wait failed in main thread\n";
    }

    t.join();
}

// super simple thread-safe queue
// max one element!
struct msg_queue {
    std::deque<int> q{};
    std::mutex m{};

    auto push(int x) {
        auto l = std::lock_guard{m};
        q.push_back(x);
        CHECK(q.size() <= 2);
    }

    auto try_pop() -> std::optional<int> {
        auto l = std::lock_guard{m};
        if (q.empty()) {
            return {};
        }
        int x = q.front();
        q.pop_front();
        return x;
    }
};

namespace {
auto rng = [] {
    std::array<int, std::mt19937::state_size> seed_data;
    std::random_device r;
    std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
    return std::mt19937{seq};
};

std::mutex print_mut{};
auto print(auto... args) {
    std::lock_guard l{print_mut};
    ((std::cout << std::this_thread::get_id() << ": ") << ... << args)
        << std::endl;
}
} // namespace

TEST_CASE("jeff2", "[jeff]") {
    using namespace async;

    msg_queue qa{};
    msg_queue qb{};
    std::atomic<bool> a_listening{};
    std::atomic<bool> b_listening{};

    std::atomic<bool> interrupt_a{};
    std::atomic<bool> interrupt_b{};

    auto r_a = rng();
    auto service_a = [&] {
        if (auto x = qa.try_pop(); x) {
            print("a received ", *x, " from b");
            if (a_listening.exchange(false)) {
                if (std::uniform_int_distribution{0, 255}(r_a) < 128) {
                    run_triggers<"a1">(*x);
                } else {
                    run_triggers<"a2">(*x);
                }
            }
        }
    };

    // worker thread A: simulating an ISR on A receiving messages from B
    std::jthread worker_a{[&](std::stop_token st) {
        while (not st.stop_requested()) {
            if (interrupt_a.exchange(false)) {
                service_a();
            }
        }
    }};

    auto r_b = rng();
    auto service_b = [&] {
        if (auto x = qb.try_pop(); x) {
            print("b received ", *x, " from a");
            if (b_listening.exchange(false)) {
                if (std::uniform_int_distribution{0, 255}(r_b) < 128) {
                    run_triggers<"b1">(*x);
                } else {
                    run_triggers<"b2">(*x);
                }
            }
        }
    };

    // worker thread B: simulating an ISR on B receiving messages from A
    std::jthread worker_b{[&](std::stop_token st) {
        while (not st.stop_requested()) {
            if (interrupt_b.exchange(false)) {
                service_b();
            }
        }
    }};

    auto send_to_a = [&](int x) {
        print("b sending ", x, " to a");
        qa.push(x);
        interrupt_a = true;
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(200ms);
    };
    auto send_to_b = [&](int x) {
        print("a sending ", x, " to b");
        qb.push(x);
        interrupt_b = true;
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(200ms);
    };

    std::atomic<int> a_count{};
    std::atomic<int> b_count{};
    constexpr auto max_sends = 2;

    // A's loop sender
    auto sndr_a = when_any(trigger_scheduler<"a1", int>{}.schedule(),
                           trigger_scheduler<"a2", int>{}.schedule()) |
                  then([](auto x) {
                      print("a processing ", x);
                      // A takes a while to process the packet
                      using namespace std::chrono_literals;
                      std::this_thread::sleep_for(1s);
                      return x;
                  }) |
                  repeat_until([&](auto) { return a_count > max_sends; },
                               [&](auto) {
                                   a_listening = true;
                                   print("a ready to recv again");
                                   //  before A sends to B, reprocess the queue
                                   interrupt_a = true;
                                   send_to_b(++a_count);
                               });

    // B's loop sender
    auto sndr_b = when_any(trigger_scheduler<"b1", int>{}.schedule(),
                           trigger_scheduler<"b2", int>{}.schedule()) |
                  then([](auto x) {
                      print("b processing ", x);
                      return x;
                  }) |
                  repeat_until([&](auto) { return b_count > max_sends; },
                               [&](auto) {
                                   b_listening = true;
                                   print("b ready to recv again");
                                   interrupt_b = true;
                                   send_to_a(++b_count);
                               });

    // to synchronize the first send only when both sides are ready
    auto start_ready = [] { print("ready for first send"); };
    std::barrier start_sync_point(2, start_ready);

    std::jthread a{[&] {
        print("thread a running");
        [[maybe_unused]] auto result =
            just([&] {
                // this self-trigger starts sndr_a
                run_triggers<"start a">();
                b_listening = true;
                start_sync_point.arrive_and_wait();
                // first send: here we know both sides
                // are ready to receive
                send_to_b(a_count);
            }) |
            incite_on(trigger_scheduler<"start a">{}) | seq(sndr_a) |
            sync_wait();
        print("thread a done");
    }};

    std::jthread b{[&] {
        print("thread b running");
        [[maybe_unused]] auto result =
            just([&] {
                // this self-trigger starts sndr_b
                run_triggers<"start b">();
                a_listening = true;
                start_sync_point.arrive_and_wait();
                // first send: here we know both sides
                // are ready to receive
                send_to_a(b_count);
            }) |
            incite_on(trigger_scheduler<"start b">{}) | seq(sndr_b) |
            sync_wait();
        print("thread b done");
    }};
}
