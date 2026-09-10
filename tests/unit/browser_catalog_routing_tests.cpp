#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace browser_catalog_routing_test {
namespace session = deluge::gui::ui_session;
constexpr int CATALOG_SEARCH_LEFT = -1, CATALOG_SEARCH_RIGHT = 1;
bool shouldInterpretNoteNames = false, octaveStartsFromA = false;
int strcmpspecial(const char* a, const char* b) {
	return strcmp(a, b);
}
struct FileItem {
	const char* displayName;
};
struct text_value {
	std::string value;
	void set(const char* text) { value = text; }
	bool isEmpty() { return value.empty(); }
	const char* get() { return value.c_str(); }
};
struct file_list {
	std::vector<FileItem> items;
	int getNumElements() { return items.size(); }
	void* getElementAddress(int index) { return &items.at(index); }
	void sortForStrings() {
		std::sort(items.begin(), items.end(), [](auto a, auto b) { return strcmp(a.displayName, b.displayName) < 0; });
	}
	int search(const char* text, bool* exact = nullptr) {
		auto it = std::lower_bound(items.begin(), items.end(), text,
		                           [](auto item, auto key) { return strcmp(item.displayName, key) < 0; });
		if (exact)
			*exact = it != items.end() && !strcmp(it->displayName, text);
		return it - items.begin();
	}
};
struct Browser {
	struct SessionState {
		int32_t max_file_items{}, catalog_search_direction{};
		file_list files;
		int32_t deleted_start{}, deleted_end{};
		text_value first, last;
		const char* search_start = nullptr;
	};
	static SessionState& session_state();
	static int32_t& max_file_items_for_session();
	static int32_t& catalog_search_direction_for_session();
	bool shouldInterpretNoteNamesForThisBrowser = false;
	file_list& file_items_for_session() { return session_state().files; }
	int32_t& num_file_items_deleted_at_start_for_session() { return session_state().deleted_start; }
	int32_t& num_file_items_deleted_at_end_for_session() { return session_state().deleted_end; }
	text_value& first_file_item_remaining_for_session() { return session_state().first; }
	text_value& last_file_item_remaining_for_session() { return session_state().last; }
	const char* filename_to_start_search_at_for_session() { return session_state().search_start; }
	void deleteSomeFileItems(int start, int end) {
		auto& items = session_state().files.items;
		items.erase(items.begin() + start, items.begin() + end);
	}
	void sortFileItems();
	void cullSomeFileItems();
};
#include "browser_catalog_routing.inc"
TEST_GROUP(BrowserCatalogRouting){
    void setup() override{for (auto owner : {session::Id::Local, session::Id::Remote}){session::Scope scope(owner);
Browser::session_state() = {};
Browser::session_state().files.items = {{"F"}, {"B"}, {"E"}, {"A"}, {"D"}, {"C"}};
} // namespace browser_catalog_routing_test
}
}
;
TEST(BrowserCatalogRouting, opposite_searches_preserve_each_sessions_results) {
	{
		session::Scope local(session::Id::Local);
		Browser::catalog_search_direction_for_session() = CATALOG_SEARCH_LEFT;
		Browser::session_state().search_start = "D";
	}
	{
		session::Scope remote(session::Id::Remote);
		Browser::catalog_search_direction_for_session() = CATALOG_SEARCH_RIGHT;
		Browser::session_state().search_start = "B";
		Browser{}.sortFileItems();
		LONGS_EQUAL(4, Browser::session_state().files.getNumElements());
		STRCMP_EQUAL("C", Browser::session_state().files.items.front().displayName);
		LONGS_EQUAL(2, Browser::session_state().deleted_start);
	}
	session::Scope local(session::Id::Local);
	Browser{}.sortFileItems();
	LONGS_EQUAL(3, Browser::session_state().files.getNumElements());
	STRCMP_EQUAL("C", Browser::session_state().files.items.back().displayName);
	LONGS_EQUAL(3, Browser::session_state().deleted_end);
	LONGS_EQUAL(0, Browser::session_state().deleted_start);
}
TEST(BrowserCatalogRouting, interleaved_culls_keep_independent_budgets_and_boundaries) {
	{
		session::Scope local(session::Id::Local);
		Browser::max_file_items_for_session() = 4;
		Browser::catalog_search_direction_for_session() = CATALOG_SEARCH_LEFT;
	}
	{
		session::Scope remote(session::Id::Remote);
		Browser::max_file_items_for_session() = 8;
		Browser::catalog_search_direction_for_session() = CATALOG_SEARCH_RIGHT;
		Browser{}.cullSomeFileItems();
		LONGS_EQUAL(4, Browser::session_state().files.getNumElements());
		STRCMP_EQUAL("D", Browser::session_state().last.get());
		LONGS_EQUAL(2, Browser::session_state().deleted_end);
	}
	{
		session::Scope local(session::Id::Local);
		Browser{}.cullSomeFileItems();
		LONGS_EQUAL(2, Browser::session_state().files.getNumElements());
		STRCMP_EQUAL("E", Browser::session_state().first.get());
		LONGS_EQUAL(4, Browser::session_state().deleted_start);
		CHECK(Browser::session_state().last.isEmpty());
	}
	session::Scope remote(session::Id::Remote);
	LONGS_EQUAL(4, Browser::session_state().files.getNumElements());
	CHECK(Browser::session_state().first.isEmpty());
}
} // namespace browser_catalog_routing_test
