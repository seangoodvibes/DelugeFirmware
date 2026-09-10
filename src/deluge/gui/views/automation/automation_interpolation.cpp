#include "gui/views/automation/automation_interpolation.h"
#include "gui/views/automation_view.h"

void get_automation_interpolation(bool& before, bool& after) {
	const bool editing = getRootUI() == &automation_view_for_session();
	before = editing && automation_view_for_session().interpolationBefore;
	after = editing && automation_view_for_session().interpolationAfter;
}
