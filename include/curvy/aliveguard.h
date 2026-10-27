#pragma once

#include <newui/delegate.h>

#include <memory>
#include <utility>

namespace curvy {

    // Wraps a delegate handler that captures its owner: the views and models outlive the controllers and still fire while the workspace
    // closes, so the handler does nothing once the owner has cleared `alive` (in its destructor).
    template <class Fn>
    auto guarded(std::shared_ptr<bool> alive, Fn fn) {
        return [alive = std::move(alive), fn = std::move(fn)](auto&&... args) {
            if (!*alive) {
                return newui::SyncReturn::Ignored;
            }
            return fn(std::forward<decltype(args)>(args)...);
        };
    }

}
