#include "vpp/runtime/heap.h"

#include <algorithm>
#include <unordered_set>

namespace vietvm::runtime {
namespace {

thread_local RuntimeHeap *activeHeap = nullptr;

template <typename T>
void trackWeak(std::vector<std::weak_ptr<T>> &registry,
               const std::shared_ptr<T> &value) {
    if (value == nullptr) return;
    for (auto it = registry.begin(); it != registry.end();) {
        const std::shared_ptr<T> live = it->lock();
        if (live == nullptr) {
            it = registry.erase(it);
            continue;
        }
        if (live.get() == value.get()) return;
        ++it;
    }
    registry.push_back(value);
}

template <typename T>
void pruneExpired(std::vector<std::weak_ptr<T>> &registry) {
    registry.erase(
        std::remove_if(registry.begin(), registry.end(),
                       [](const std::weak_ptr<T> &entry) { return entry.expired(); }),
        registry.end());
}

template <typename T>
std::size_t liveCount(const std::vector<std::weak_ptr<T>> &registry) {
    return static_cast<std::size_t>(std::count_if(
        registry.begin(), registry.end(),
        [](const std::weak_ptr<T> &entry) { return !entry.expired(); }));
}

void markValue(const StackValue &value,
               std::unordered_set<const void *> &marked);

void markClass(const ClassHandle &klass,
               std::unordered_set<const void *> &marked) {
    if (klass == nullptr || !marked.insert(klass.get()).second) return;
    markClass(klass->superclass, marked);
}

void markInstance(const InstanceHandle &instance,
                  std::unordered_set<const void *> &marked) {
    if (instance == nullptr || !marked.insert(instance.get()).second) return;
    markClass(instance->klass, marked);
    for (const auto &field : instance->fields) markValue(field.second, marked);
}

void markClosure(const ClosureHandle &closure,
                 std::unordered_set<const void *> &marked) {
    if (closure == nullptr || !marked.insert(closure.get()).second) return;
    for (const auto &capture : closure->captures) {
        if (capture.second != nullptr) markValue(capture.second->value, marked);
    }
}

void markValue(const StackValue &value,
               std::unordered_set<const void *> &marked) {
    if (std::holds_alternative<MapHandle>(value)) {
        const MapHandle &map = std::get<MapHandle>(value);
        if (map == nullptr || !marked.insert(map.get()).second) return;
        for (const auto &entry : map->entries) markValue(entry.second, marked);
        return;
    }
    if (std::holds_alternative<ListHandle>(value)) {
        const ListHandle &list = std::get<ListHandle>(value);
        if (list == nullptr || !marked.insert(list.get()).second) return;
        for (const StackValue &element : list->elements) markValue(element, marked);
        return;
    }
    if (std::holds_alternative<TupleHandle>(value)) {
        const TupleHandle &tuple = std::get<TupleHandle>(value);
        if (tuple == nullptr || !marked.insert(tuple.get()).second) return;
        for (const StackValue &element : tuple->elements) markValue(element, marked);
        return;
    }
    if (std::holds_alternative<ClassHandle>(value)) {
        markClass(std::get<ClassHandle>(value), marked);
        return;
    }
    if (std::holds_alternative<InstanceHandle>(value)) {
        markInstance(std::get<InstanceHandle>(value), marked);
        return;
    }
    if (std::holds_alternative<ClosureHandle>(value)) {
        markClosure(std::get<ClosureHandle>(value), marked);
    }
}

template <typename T, typename Clear>
std::size_t sweepRegistry(std::vector<std::weak_ptr<T>> &registry,
                          const std::unordered_set<const void *> &marked,
                          Clear clearEdges) {
    std::size_t swept = 0;
    for (const std::weak_ptr<T> &entry : registry) {
        const std::shared_ptr<T> value = entry.lock();
        if (value == nullptr || marked.count(value.get()) != 0) continue;
        clearEdges(*value);
        ++swept;
    }
    pruneExpired(registry);
    return swept;
}

} // namespace

RuntimeHeap::~RuntimeHeap() {
    (void)collect({});
}

void RuntimeHeap::track(const MapHandle &value) { trackWeak(maps_, value); }
void RuntimeHeap::track(const ListHandle &value) { trackWeak(lists_, value); }
void RuntimeHeap::track(const TupleHandle &value) { trackWeak(tuples_, value); }
void RuntimeHeap::track(const ClassHandle &value) { trackWeak(classes_, value); }
void RuntimeHeap::track(const InstanceHandle &value) { trackWeak(instances_, value); }
void RuntimeHeap::track(const ClosureHandle &value) { trackWeak(closures_, value); }

void RuntimeHeap::trackValue(const StackValue &value) {
    std::unordered_set<const void *> visited;
    const auto walk = [&](const auto &self, const StackValue &current) -> void {
        if (std::holds_alternative<MapHandle>(current)) {
            const MapHandle &map = std::get<MapHandle>(current);
            if (map == nullptr || !visited.insert(map.get()).second) return;
            track(map);
            for (const auto &entry : map->entries) self(self, entry.second);
            return;
        }
        if (std::holds_alternative<ListHandle>(current)) {
            const ListHandle &list = std::get<ListHandle>(current);
            if (list == nullptr || !visited.insert(list.get()).second) return;
            track(list);
            for (const StackValue &element : list->elements) self(self, element);
            return;
        }
        if (std::holds_alternative<TupleHandle>(current)) {
            const TupleHandle &tuple = std::get<TupleHandle>(current);
            if (tuple == nullptr || !visited.insert(tuple.get()).second) return;
            track(tuple);
            for (const StackValue &element : tuple->elements) self(self, element);
            return;
        }
        if (std::holds_alternative<ClassHandle>(current)) {
            ClassHandle klass = std::get<ClassHandle>(current);
            while (klass != nullptr && visited.insert(klass.get()).second) {
                track(klass);
                klass = klass->superclass;
            }
            return;
        }
        if (std::holds_alternative<InstanceHandle>(current)) {
            const InstanceHandle &instance = std::get<InstanceHandle>(current);
            if (instance == nullptr || !visited.insert(instance.get()).second) return;
            track(instance);
            if (instance->klass != nullptr) {
                self(self, make_class_value(instance->klass));
            }
            for (const auto &field : instance->fields) self(self, field.second);
            return;
        }
        if (!std::holds_alternative<ClosureHandle>(current)) return;
        const ClosureHandle &closure = std::get<ClosureHandle>(current);
        if (closure == nullptr || !visited.insert(closure.get()).second) return;
        track(closure);
        for (const auto &capture : closure->captures) {
            if (capture.second != nullptr) self(self, capture.second->value);
        }
    };
    walk(walk, value);
}

RuntimeHeapStats RuntimeHeap::collect(const std::vector<StackValue> &roots) {
    pruneExpired(maps_);
    pruneExpired(lists_);
    pruneExpired(tuples_);
    pruneExpired(classes_);
    pruneExpired(instances_);
    pruneExpired(closures_);

    RuntimeHeapStats stats;
    stats.trackedBefore = maps_.size() + lists_.size() + tuples_.size() +
                          classes_.size() + instances_.size() + closures_.size();

    std::unordered_set<const void *> marked;
    for (const StackValue &root : roots) markValue(root, marked);
    stats.marked = marked.size();

    stats.swept += sweepRegistry(
        maps_, marked, [](MapValue &value) { value.entries.clear(); });
    stats.swept += sweepRegistry(
        lists_, marked, [](ListValue &value) { value.elements.clear(); });
    stats.swept += sweepRegistry(
        tuples_, marked, [](TupleValue &value) { value.elements.clear(); });
    stats.swept += sweepRegistry(
        instances_, marked, [](RuntimeInstance &value) {
            value.fields.clear();
            value.klass.reset();
        });
    stats.swept += sweepRegistry(
        classes_, marked, [](RuntimeClass &value) {
            value.superclass.reset();
            value.methods.clear();
        });
    stats.swept += sweepRegistry(
        closures_, marked, [](RuntimeClosure &value) { value.captures.clear(); });

    pruneExpired(maps_);
    pruneExpired(lists_);
    pruneExpired(tuples_);
    pruneExpired(classes_);
    pruneExpired(instances_);
    pruneExpired(closures_);
    stats.trackedAfter = maps_.size() + lists_.size() + tuples_.size() +
                         classes_.size() + instances_.size() + closures_.size();
    return stats;
}

std::size_t RuntimeHeap::trackedObjectCount() {
    pruneExpired(maps_);
    pruneExpired(lists_);
    pruneExpired(tuples_);
    pruneExpired(classes_);
    pruneExpired(instances_);
    pruneExpired(closures_);
    return liveCount(maps_) + liveCount(lists_) + liveCount(tuples_) +
           liveCount(classes_) + liveCount(instances_) + liveCount(closures_);
}

RuntimeHeapScope::RuntimeHeapScope(RuntimeHeap &heap) noexcept
    : previous_(activeHeap) {
    activeHeap = &heap;
}

RuntimeHeapScope::~RuntimeHeapScope() { activeHeap = previous_; }

void trackRuntimeAllocation(const MapHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const ListHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const TupleHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const ClassHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const InstanceHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const ClosureHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

} // namespace vietvm::runtime
