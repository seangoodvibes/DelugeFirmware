#include "../tracking_allocator.h"
#include "cppspec.hpp"
#include "memory/object_pool.h"
#include <cassert>
#include <memory>

namespace {
struct owned_resource {
	inline static int live_resources = 0;
	int value;
	explicit owned_resource(int initial_value) : value(initial_value) { ++live_resources; }
	~owned_resource() { --live_resources; }
};

struct tracked_object {
	inline static int constructions = 0;
	inline static int destructions = 0;
	std::unique_ptr<owned_resource> resource;
	explicit tracked_object(int value) : resource(std::make_unique<owned_resource>(value)) { ++constructions; }
	~tracked_object() { ++destructions; }
};
using lifetime_pool = deluge::memory::ObjectPool<tracked_object, TrackingAllocator>;
} // namespace

// clang-format off
describe object_pool_lifetime("ObjectPool nontrivial lifetimes", $ {
	before_each([] {
		auto& pool = lifetime_pool::get();
		pool.clear();
		assert(owned_resource::live_resources == 0);
		TrackingAllocator<tracked_object>::reset();
		tracked_object::constructions = 0;
		tracked_object::destructions = 0;
		pool.resize(2);
		pool.repopulate();
	});

	after_each([] {
		lifetime_pool::get().clear();
		assert(tracked_object::constructions == tracked_object::destructions);
		assert(owned_resource::live_resources == 0);
		assert(TrackingAllocator<tracked_object>::num_outstanding() == 0);
	});

	it("reserves and frees raw storage without constructing or destroying objects", _{
		auto& pool = lifetime_pool::get();
		expect(pool.size()).to_equal(2);
		expect(tracked_object::constructions).to_equal(0);
		pool.resize(1);
		pool.clear();
		expect(tracked_object::destructions).to_equal(0);
		expect(owned_resource::live_resources).to_equal(0);
	});

	it("destroys owned resources on release and reconstructs fresh state on reuse", _{
		auto& pool = lifetime_pool::get();
		tracked_object* address;
		{
			auto object = pool.acquire(17);
			address = object.get();
			expect(object->resource->value).to_equal(17);
			expect(owned_resource::live_resources).to_equal(1);
			object->resource->value = -42;
		}
		expect(tracked_object::destructions).to_equal(1);
		expect(owned_resource::live_resources).to_equal(0);
		{
			auto object = pool.acquire(99);
			expect(object.get()).to_equal(address);
			expect(object->resource->value).to_equal(99);
			expect(tracked_object::constructions).to_equal(2);
			expect(owned_resource::live_resources).to_equal(1);
		}
		expect(tracked_object::destructions).to_equal(2);
	});

	it("moving the managed pointer transfers ownership without double destruction", _{
		auto object = lifetime_pool::get().acquire(17);
		auto moved_object = std::move(object);
		expect(object.get() == nullptr).to_be_true();
		expect(tracked_object::constructions).to_equal(1);
		expect(tracked_object::destructions).to_equal(0);
		moved_object.reset();
		expect(tracked_object::destructions).to_equal(1);
		expect(owned_resource::live_resources).to_equal(0);
	});

	it("clearing cached storage leaves an acquired object alive until release", _{
		auto& pool = lifetime_pool::get();
		auto object = pool.acquire(17);
		pool.clear();
		expect(object->resource->value).to_equal(17);
		expect(tracked_object::destructions).to_equal(0);
		expect(owned_resource::live_resources).to_equal(1);
		object.reset();
		expect(pool.size()).to_equal(1);
		expect(tracked_object::destructions).to_equal(1);
	});

	it("shrinking to zero frees a returned object after destroying its resource", _{
		auto& pool = lifetime_pool::get();
		auto object = pool.acquire(17);
		auto* address = object.get();
		pool.resize(0);
		expect(tracked_object::destructions).to_equal(0);
		object.reset();
		expect(pool.empty()).to_be_true();
		expect(tracked_object::destructions).to_equal(1);
		expect(owned_resource::live_resources).to_equal(0);
		expect(TrackingAllocator<tracked_object>::is_deallocated(address)).to_be_true();
	});

	it("returning to a full pool destroys the object and frees its storage", _{
		auto& pool = lifetime_pool::get();
		auto object = pool.acquire(17);
		auto* address = object.get();
		pool.repopulate();
		object.reset();
		expect(pool.size()).to_equal(2);
		expect(tracked_object::destructions).to_equal(1);
		expect(owned_resource::live_resources).to_equal(0);
		expect(TrackingAllocator<tracked_object>::is_deallocated(address)).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(object_pool_lifetime);
