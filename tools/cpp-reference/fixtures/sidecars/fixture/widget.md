\file fixture/widget.h
Header used to prove the reference coverage gate.

\namespace fixture
Namespace of the coverage fixture.

\enum fixture::Mode
Mode of a widget.

\var fixture::Mode::kFast
Fast mode.

\var fixture::Mode::kSafe
Safe mode.

\class fixture::Widget
Resizable widget.

\fn int fixture::Widget::Size() const
Returns the size.

\fn void fixture::Widget::Resize(int size)
Resizes the widget to a square.

\fn void fixture::Widget::Resize(int width, int height)
Resizes the widget to a rectangle.

\fn int fixture::Scale(int value)
Scales an integer.

\fn int fixture::Scale(double value)
Scales a floating-point value.
