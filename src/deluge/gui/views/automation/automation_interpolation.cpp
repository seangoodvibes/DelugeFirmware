#include "gui/views/automation/automation_interpolation.h"
#include "gui/views/automation_view.h"

void get_automation_interpolation(bool& before, bool& after) {
	const bool editing = getRootUI() == &automationView;
	before = editing && automationView.interpolationBefore;
	after = editing && automationView.interpolationAfter;
}
