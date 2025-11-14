#pragma once

#include <deque>
#include <functional>

namespace mnv {
    struct DeletionQueue {
        using Deletor = std::function<void()>;

        std::deque<Deletor> deletors;

        void pushFunction(Deletor&& function) {
            deletors.push_back(function);
        }

        void flush() {
            // iterate in reverse
            for (auto itr = deletors.rbegin(); itr != deletors.rend(); ++itr) {
                (void)(*itr)();
            }
            deletors.clear();
        }
    };
}
