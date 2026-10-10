# Display-switch routing tests

This target compiles the extracted production `swapDisplayType` function with
real UI-session scopes and lightweight display/UI fixtures. It checks both display
transitions, Remote callers, construction/destruction ownership, redraw routing,
and UI replacement/removal during the notification callback.

These tests do not cover real display allocation failure, destructor re-entry,
SPI/PIC output or the full menu/input stack. Those remain separate lifetime and
hardware-validation concerns.
