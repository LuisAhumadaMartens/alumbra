#ifndef FIXTURE_WIDGET_H_
#define FIXTURE_WIDGET_H_

namespace fixture {

enum class Mode { kFast, kSafe };

class Widget {
 public:
  Widget() = default;

  [[nodiscard]] int Size() const;
  void Resize(int size);
  void Resize(int width, int height);

 private:
  int size_ = 0;
};

int Scale(int value);
int Scale(double value);

}

#endif
