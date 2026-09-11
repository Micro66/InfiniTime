#include "PlayLogic.h"
#include <cassert>
#include <iostream>

int main() {
  using namespace Esp32;
  assert((ScreenTilt(.02f, -.04f, .02f, -.04f) == std::array<float, 2> {0, 0}));
  assert((ScreenTilt(1, 0, 0, 0) == std::array<float, 2> {0, 1})); // Upright text: roll down.
  assert((ScreenTilt(-1, 0, 0, 0) == std::array<float, 2> {0, -1}));
  assert((ScreenTilt(0, 1, 0, 0) == std::array<float, 2> {-1, 0}));
  assert((ScreenTilt(0, -1, 0, 0) == std::array<float, 2> {1, 0}));
  FocusClock clock;
  clock.Toggle(100);
  assert(clock.running && clock.phase == FocusClock::Phase::Focus);
  clock.Tick(200);
  clock.Toggle(200);
  assert(!clock.running && clock.remaining == 1400);
  clock.Tick(10000);
  assert(clock.remaining == 1400);
  clock.Toggle(10000);
  assert(clock.deadline == 11400);
  assert(!clock.Tick(11399) && clock.flowers == 0);
  assert(clock.Tick(11400) && clock.flowers == 1);
  for (int i = 0; i < 100; ++i)
    assert(!clock.Tick(20000 + i));
  assert(clock.flowers == 1 && clock.phase == FocusClock::Phase::Complete);
  clock.Toggle(20100);
  assert(clock.phase == FocusClock::Phase::Rest && clock.remaining == 300);
  assert(clock.Tick(20400) && clock.flowers == 1 && clock.phase == FocusClock::Phase::Ready);
  clock.Toggle(30000);
  clock.Tick(30001);
  clock.Reset();
  assert(clock.flowers == 1 && !clock.running);
  clock.Toggle(UINT64_C(5000000000));
  assert(clock.Tick(UINT64_C(5000001500)) && clock.flowers == 2);

  MarblePhysics ball;
  ball.x = 414;
  ball.y = 233;
  ball.vx = 200;
  ball.Step(0, 0, .02);
  assert(ball.x <= 416 && ball.vx < 0);
  ball = {};
  ball.x = 170;
  ball.y = 150;
  ball.vy = 100;
  ball.Step(0, 0, .02);
  assert(ball.vy < 0 && ball.score == 10);
  for (int i = 0; i < 20000; ++i) {
    ball.Step(std::sin(i * .01), std::cos(i * .013), 1.0f / 120);
    assert(std::isfinite(ball.x) && std::isfinite(ball.y));
    assert(std::hypot(ball.x - 233, ball.y - 233) <= 183.01);
    for (auto& bumper : MarblePhysics::bumpers)
      assert(std::hypot(ball.x - bumper[0], ball.y - bumper[1]) >= 33.99);
  }
  int16_t samples[256] {};
  for (auto level : Spectrum(samples, 256))
    assert(level == 0);
  for (auto& sample : samples)
    sample = 2000;
  for (auto level : Spectrum(samples, 256))
    assert(level < .0001);
  for (int i = 0; i < 256; ++i)
    samples[i] = std::lround(16000 * std::sin(2 * 3.14159265359 * 1000 * i / 16000));
  auto spectrum = Spectrum(samples, 256);
  assert(spectrum[3] > .47 && spectrum[3] < .50);
  for (int i = 0; i < 8; ++i)
    if (i != 3)
      assert(spectrum[i] < .01);
  std::cout << "PASS: focus pause/resume/completion/rest, physics boundaries/collisions, actual spectrum silence/DC/tone\n";
}
