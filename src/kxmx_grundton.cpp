#include <kxmx_bluemchen.h>
#include <q/fx/signal_conditioner.hpp>
#include <q/pitch/pitch_detector.hpp>
#include <string.h>
#include <util/CpuLoadMeter.h>

using namespace kxmx;
using namespace daisy;

namespace q = cycfi::q;
using namespace q::literals;

constexpr auto sps = 48000;

Bluemchen bluemchen;
CpuLoadMeter load_meter;

float frequency_l;
float frequency_r;
float frequency;

float envelope_l;
float envelope_r;
float envelope;

q::frequency lowest_frequency = 80_Hz;
q::frequency highest_frequency = 500_Hz;

q::frequency eq_low_frequency = 135_Hz;
q::frequency eq_mid_frequency = 2.58_kHz;
q::frequency eq_high_frequency = 5.37_kHz;

q::highpass high_pass_filter_l(lowest_frequency, sps);
q::highpass high_pass_filter_r(lowest_frequency, sps);

q::lowpass low_pass_filter_l(highest_frequency, sps);
q::lowpass low_pass_filter_r(highest_frequency, sps);

q::peaking eq_filter_low(-10.0f, eq_low_frequency, sps, 0.4f);
q::peaking eq_filter_mid(3.0f, eq_mid_frequency, sps, 0.8f);
q::peaking eq_filter_high(1.0f, eq_high_frequency, sps, 0.6f);

q::fast_envelope_follower envelope_follower_l(lowest_frequency.period() * 0.6,
                                              sps);
q::fast_envelope_follower envelope_follower_r(lowest_frequency.period() * 0.6,
                                              sps);

q::pitch_detector pitch_detector_l(lowest_frequency, highest_frequency, sps,
                                   -45_dB);
q::pitch_detector pitch_detector_r(lowest_frequency, highest_frequency, sps,
                                   -45_dB);

void UpdateOled() {
  bluemchen.display.Fill(false);

  bluemchen.display.SetCursor(0, 0);
  std::string str = "Frq: ";
  char *cstr = &str[0];
  bluemchen.display.WriteString(cstr, Font_6x8, true);

  str = std::to_string(static_cast<int>(frequency * 100.0f));
  bluemchen.display.SetCursor(30, 0);
  bluemchen.display.WriteString(cstr, Font_6x8, true);

  str = "Env: ";
  bluemchen.display.SetCursor(0, 8);
  bluemchen.display.WriteString(cstr, Font_6x8, true);

  str = std::to_string(static_cast<int>(envelope * 100.0f));
  bluemchen.display.SetCursor(30, 8);
  bluemchen.display.WriteString(cstr, Font_6x8, true);

  str = "Lod: ";
  bluemchen.display.SetCursor(0, 24);
  bluemchen.display.WriteString(cstr, Font_6x8, true);

  str = std::to_string(static_cast<int>(load_meter.GetAvgCpuLoad() * 100.0f));
  bluemchen.display.SetCursor(30, 24);
  bluemchen.display.WriteString(cstr, Font_6x8, true);

  bluemchen.display.Update();
}

void UpdateControls() { 
  bluemchen.ProcessAllControls(); 
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  load_meter.OnBlockStart();
  UpdateControls();
  for (size_t i = 0; i < size; i++) {
    float sig_l = in[0][i];
    float sig_r = in[1][i];
    float pd_sig_l;
    float pd_sig_r;

    float eq_sig_l;
    // float eq_sig_r;


    // Envelope followers
    envelope_l = envelope_follower_l(abs(sig_l));
    envelope_r = envelope_follower_r(abs(sig_r));

    // Basic signal conditioning
    pd_sig_l = high_pass_filter_l(sig_l);
    pd_sig_l = low_pass_filter_l(pd_sig_l);

    pd_sig_r = high_pass_filter_r(sig_r);
    pd_sig_r = low_pass_filter_r(pd_sig_r);

    if (pitch_detector_l(pd_sig_l)) {
      auto f = pitch_detector_l.get_frequency();
      if (f != 0.0f) {
        frequency_l = f;
      }
    }

    if (pitch_detector_r(pd_sig_r)) {
      auto f = pitch_detector_r.get_frequency();
      if (f != 0.0f) {
        frequency_r = f;
      }
    }

    // auto frequency_delta = abs(frequency_l - frequency_r);
    // if (frequency_delta < 0.5f) {
    //   frequency = (frequency_l > frequency_r)
    //                   ? frequency_l - (frequency_delta / 2)
    //                   : frequency_r - (frequency_delta / 2);
    // }

    frequency = frequency_l;
    envelope = envelope_l;


    eq_sig_l = eq_filter_low(sig_l);
    eq_sig_l = eq_filter_mid(eq_sig_l);
    eq_sig_l = eq_filter_high(eq_sig_l);


    out[0][i] = sig_l;
    out[1][i] = eq_sig_l;
  }
  load_meter.OnBlockEnd();
}

int main(void) {
  bluemchen.Init();
  bluemchen.StartAdc();

  load_meter.Init(bluemchen.seed.AudioSampleRate(),
                  bluemchen.seed.AudioBlockSize());

  bluemchen.StartAudio(AudioCallback);

  while (1) {
    UpdateOled();
  }
}
