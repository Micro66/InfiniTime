#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Esp32 {
  // 1.75C at display rotation 0, established with physical tilt tests:
  // sensor +X rolls down the screen, sensor +Y rolls right.
  inline std::array<float, 2> ScreenTilt(float x, float y, float zeroX, float zeroY) {
    return {std::clamp(y - zeroY, -1.0f, 1.0f), std::clamp(x - zeroX, -1.0f, 1.0f)};
  }

  struct FocusClock {
    enum class Phase : uint8_t { Ready, Focus, Rest, Complete };
    Phase phase = Phase::Ready;
    bool running = false;
    uint32_t duration = 25 * 60, remaining = duration, flowers = 0;
    uint64_t deadline = 0;

    void Toggle(uint64_t now) {
      Tick(now);
      if (phase == Phase::Complete) {
        phase = Phase::Rest;
        remaining = 5 * 60;
      } else if (phase == Phase::Ready)
        phase = Phase::Focus;
      running = !running;
      if (running)
        deadline = now + remaining;
    }

    bool Tick(uint64_t now) {
      if (!running)
        return false;
      remaining = now < deadline ? static_cast<uint32_t>(deadline - now) : 0;
      if (remaining)
        return false;
      running = false;
      if (phase == Phase::Focus) {
        ++flowers;
        phase = Phase::Complete;
      } else {
        phase = Phase::Ready;
        remaining = duration;
      }
      return true;
    }

    void Reset() {
      running = false;
      phase = Phase::Ready;
      remaining = duration;
    }
  };

  struct MarblePhysics {
    float x = 233, y = 320, vx = 0, vy = 0;
    unsigned score = 0;
    float hitCooldown[3] {};
    static constexpr float bumpers[3][2] {{170, 185}, {296, 185}, {233, 265}};

    void Step(float ax, float ay, float dt) {
      vx = std::clamp((vx + ax * 430 * dt) * std::exp(-0.35f * dt), -400.0f, 400.0f);
      vy = std::clamp((vy + ay * 430 * dt) * std::exp(-0.35f * dt), -400.0f, 400.0f);
      x += vx * dt;
      y += vy * dt;
      float dx = x - 233, dy = y - 233, distance = std::hypot(dx, dy);
      if (distance > 183) {
        const float nx = dx / distance, ny = dy / distance;
        x = 233 + nx * 183;
        y = 233 + ny * 183;
        const float speed = vx * nx + vy * ny;
        if (speed > 0) {
          vx -= 1.75f * speed * nx;
          vy -= 1.75f * speed * ny;
        }
      }
      for (unsigned i = 0; i < 3; ++i) {
        hitCooldown[i] = std::max(0.0f, hitCooldown[i] - dt);
        dx = x - bumpers[i][0];
        dy = y - bumpers[i][1];
        distance = std::hypot(dx, dy);
        if (distance < 34) {
          const float nx = distance > 0.001f ? dx / distance : 0;
          const float ny = distance > 0.001f ? dy / distance : 1;
          x = bumpers[i][0] + nx * 34;
          y = bumpers[i][1] + ny * 34;
          const float speed = vx * nx + vy * ny;
          if (speed < 0) {
            vx -= 1.9f * speed * nx;
            vy -= 1.9f * speed * ny;
            if (hitCooldown[i] == 0) {
              score += 10;
              hitCooldown[i] = 0.3f;
            }
          }
        }
      }
    }
  };

  // Hann-windowed Goertzel bins, normalized to PCM full scale.
  inline std::array<float, 8> Spectrum(const int16_t* samples, size_t count) {
    constexpr float pi = 3.14159265359f;
    constexpr float frequencies[] {125, 250, 500, 1000, 2000, 3000, 4000, 6000};
    std::array<float, 8> result {};
    if (count < 2)
      return result;
    float mean = 0;
    for (size_t n = 0; n < count; ++n)
      mean += samples[n];
    mean /= count;
    for (unsigned bin = 0; bin < result.size(); ++bin) {
      const float coefficient = 2 * std::cos(2 * pi * frequencies[bin] / 16000);
      float s1 = 0, s2 = 0;
      for (size_t n = 0; n < count; ++n) {
        const float window = 0.5f - 0.5f * std::cos(2 * pi * n / (count - 1));
        const float s = (samples[n] - mean) / 32768 * window + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
      }
      result[bin] = std::sqrt(std::max(0.0f, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) * 4 / count;
    }
    return result;
  }
}
