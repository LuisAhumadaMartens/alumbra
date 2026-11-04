\file alumbra/desktop_ui/desktop_window.h
Desktop window and UI host for a player on desktop builds.

\namespace alumbra::desktop_ui
Desktop window and control hosts for desktop builds.

\struct alumbra::desktop_ui::DesktopWindowOptions
Typed options for the host-owned desktop window.

\var alumbra::desktop_ui::DesktopWindowOptions::title
Window title.

\var alumbra::desktop_ui::DesktopWindowOptions::width
Initial window width in pixels. Must be positive.

\var alumbra::desktop_ui::DesktopWindowOptions::height
Initial window height in pixels. Must be positive.

\var alumbra::desktop_ui::DesktopWindowOptions::fullscreen
Whether the newly created window starts in fullscreen mode.

\class alumbra::desktop_ui::DesktopWindow
Owns the desktop window, platform event loop and UI wiring for one player. SDL and presenter types remain internal.

\fn static std::unique_ptr<DesktopWindow> alumbra::desktop_ui::DesktopWindow::Create(umbra::player::Player& player, const DesktopWindowOptions& options)
Creates a desktop host and attaches its owned window to the player's desktop port. Returns null when the window,
port attachment or UI renderer cannot be created.

\fn bool alumbra::desktop_ui::DesktopWindow::PumpEvents()
Pumps platform events and returns false when the host should stop running.

\fn bool alumbra::desktop_ui::DesktopWindow::RenderFrame()
Renders a UI frame when the presenter and UI are ready. Returns whether the frame was rendered.
