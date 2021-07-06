#include <kxmx_bluemchen.h>
#include <q/fx/signal_conditioner.hpp>
#include <q/pitch/pitch_detector.hpp>
#include <string.h>

using namespace kxmx;
using namespace daisy;

namespace q = cycfi::q;
using namespace q::literals;

constexpr auto sps = 48000;

Bluemchen bluemchen;

float estFrequency;
float envelope;

q::frequency lowest_frequency = 80_Hz;
q::frequency highest_frequency = 500_Hz;

auto sc_conf = q::signal_conditioner::config{};
q::signal_conditioner sc(sc_conf, lowest_frequency, highest_frequency, sps);

q::pitch_detector pd(lowest_frequency, highest_frequency, sps, -45_dB);



void UpdateOled() {
  bluemchen.display.Fill(false);

  bluemchen.display.SetCursor(0, 0);
  std::string str = "Frq: ";
  char *cstr = &str[0];
  bluemchen.display.WriteString(cstr, Font_6x8, true);

  str = std::to_string(static_cast<int>(estFrequency));
  bluemchen.display.SetCursor(30, 0);
  bluemchen.display.WriteString(cstr, Font_6x8, true);

  bluemchen.display.Update();
}

void UpdateControls() { 
    bluemchen.ProcessAllControls(); 
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  UpdateControls();
  for (size_t i = 0; i < size; i++) {
    float sig_l = in[0][i];
    float sig_r = in[1][i];

    sig_l = sc(sig_l);
    envelope = sc.signal_env();

    if (pd(sig_l)) {
      auto frequency = pd.get_frequency();
      if (frequency != 0.0f) {
        estFrequency = frequency;
      }
    }

    out[0][i] = sig_l;
    out[1][i] = sig_r;
  }
}

int main(void) {
  bluemchen.Init();
  bluemchen.StartAdc();
  bluemchen.StartAudio(AudioCallback);

  while (1) {
    UpdateOled();
  }
}
