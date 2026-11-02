#ifndef TEST_API_CONFORMANCE_ELEMENT_HARNESS_H_
#define TEST_API_CONFORMANCE_ELEMENT_HARNESS_H_

#include <memory>
#include <string>
#include <vector>

#include "event_trace.h"
#include "player_fixture.h"
#include "umbra/player/events.h"
#include "umbra/player/player.h"

namespace umbra::player::conformance {

struct ObservedEvent {
  std::string name;
  ElementState state;
};

class EventLog {
 public:
  explicit EventLog(Player& player);

  std::vector<ObservedEvent> Take();
  void Mark(const std::string& name);

 private:
  template <typename Event>
  void Listen(Player& player);

  std::vector<ObservedEvent> events_;
  std::vector<Subscription> subscriptions_;
};

std::vector<std::string> NamesOf(const std::vector<ObservedEvent>& events);

class ElementHarness : public test::PlayerFixture {
 protected:
  void StartElement();
  void BeginLoad(int64_t duration_ms = 3000);
  std::vector<ObservedEvent> PullEvents();
  std::vector<std::string> Pull();
  std::vector<std::string> PullWithoutProgress();
  void PlayWithMarkers();
  void ReachMetadata();
  void ReachEnoughData();
  void ReachPlaying(int64_t duration_ms = 3000);
  void CompleteSeek();
  void FinishPlayback();
  double CurrentTime();
  bool RunStep(const TraceStep& step, std::string& error);

  std::unique_ptr<EventLog> log_;
};

}

#endif
