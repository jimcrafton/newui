#pragma once

#include <string>

namespace newui {

    class RootView;
    class View;

    // "" for root itself, "childViews[N]/childViews[M]/..." for a
    // descendant - index-based since View::name() has no uniqueness
    // guarantee anywhere in this codebase, into whichever ancestor's own
    // childViews() actually holds the next hop. Also returns "" if target
    // isn't reachable from root at all (not otherwise distinguishable from
    // "target is root itself" - a caller that needs to tell those apart
    // already has target/root as distinct pointers to compare directly).
    // See Bundle::writeFrame() (bundle.cpp) for the real caller - building
    // a serializable target for an Animation's KeyValue (animation.h).
    std::string computeViewPath(RootView& root, const View* target);

    // Walks path back down from root - nullptr if any segment's index is
    // out of range for its own hop, or the path is malformed. An empty
    // path resolves to &root itself. See Bundle::loadFrame() for the real
    // caller - resolving a saved Animation target back to a live View
    // once the tree it's part of already exists.
    View* resolveViewPath(RootView& root, const std::string& path);

}
