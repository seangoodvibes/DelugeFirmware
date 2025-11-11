#pragma once

class Display {
public:
	bool haveOLED() { return true; }
};

extern Display* display;
