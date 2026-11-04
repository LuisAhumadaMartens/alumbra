#include "alumbra/desktop_ui/desktop_window.h"

int main() {
  const alumbra::desktop_ui::DesktopWindowOptions options;
  return options.width > 0 && options.height > 0 ? 0 : 1;
}
