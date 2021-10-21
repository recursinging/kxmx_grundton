#include <kxmx_bluemchen.h>
#include <q/fx/signal_conditioner.hpp>
#include <q/pitch/pitch_detector.hpp>
#include <string.h>
#include <util/CpuLoadMeter.h>
#include <algorithm>

using namespace kxmx;
using namespace daisy;

namespace q = cycfi::q;
using namespace q::literals;

constexpr auto sps = 48000;

Bluemchen    bluemchen;
CpuLoadMeter load_meter;

float frequency_a;
float frequency_b;

float envelope_a;
float envelope_b;

int idx_a = 0;
int idx_b = 1;

class guitar_string
{
  public:
    guitar_string(std::string  name,
                  q::frequency lowest_frequency,
                  q::frequency highest_frequency);

    std::string               name;
    q::frequency              lowest_frequency;
    q::frequency              highest_frequency;
    q::highpass               hpf;
    q::lowpass                lpf;
    q::fast_envelope_follower envelope;
    q::pitch_detector         pitch_detector;
};

inline guitar_string::guitar_string(std::string  name,
                                    q::frequency lowest_frequency,
                                    q::frequency highest_frequency)
: name{name},
  highest_frequency{highest_frequency},
  lowest_frequency{lowest_frequency},
  hpf{lowest_frequency, sps},
  lpf{highest_frequency, sps},
  envelope{lowest_frequency.period() * 0.6, sps},
  pitch_detector{lowest_frequency, highest_frequency, sps, -45_dB}
{
}


std::vector<guitar_string> strings = {
    {"e", 73.4_Hz, 329.6_Hz},
    {"A", 98_Hz, 493.9_Hz},
    {"D", 146.8_Hz, 659.3_Hz},
    {"G", 185_Hz, 880.9_Hz},
    {"B", 246.9_Hz, 1108.7_Hz},
    {"E", 293.7_Hz, 1318.5_Hz},
    {"*", 73.4_Hz, 1318.5_Hz},
    {"*", 73.4_Hz, 1318.5_Hz},
};


void UpdateOled()
{
    bluemchen.display.Fill(false);

    bluemchen.display.SetCursor(0, 0);
    std::string str  = strings[idx_a].name ;
    char*       cstr = &str[0];
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    str = std::to_string(static_cast<int>(frequency_a * 100.0f));
    bluemchen.display.SetCursor(30, 0);
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    str = strings[idx_b].name;
    bluemchen.display.SetCursor(0, 8);
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    str = std::to_string(static_cast<int>(frequency_b * 100.0f));
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

void UpdateControls()
{
    bluemchen.ProcessAllControls();

    int32_t inc = bluemchen.encoder.Increment();

    if(inc != 0)
    {
        idx_a = std::clamp(static_cast<int>(idx_a + inc),
                           static_cast<int>(0),
                           static_cast<int>(strings.size() - 2));
        idx_b = idx_a + 1;
    }
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    load_meter.OnBlockStart();
    UpdateControls();
    for(size_t i = 0; i < size; i++)
    {
        float sig_l = in[0][i];
        float sig_r = in[1][i];
        float pd_sig_l;
        float pd_sig_r;

        // Envelope followers
        envelope_a = strings[idx_a].envelope(abs(sig_l));
        envelope_b = strings[idx_b].envelope(abs(sig_l));

        // Basic signal conditioning
        pd_sig_l = strings[idx_a].hpf(sig_l);
        pd_sig_l = strings[idx_b].hpf(pd_sig_l);

        pd_sig_r = strings[idx_a].lpf(sig_r);
        pd_sig_r = strings[idx_b].lpf(pd_sig_r);

        if(strings[idx_a].pitch_detector(pd_sig_l))
        {
            auto f = strings[idx_a].pitch_detector.get_frequency();
            if(f != 0.0f)
            {
                frequency_a = f;
            }
        }

        if(strings[idx_b].pitch_detector(pd_sig_r))
        {
            auto f = strings[idx_b].pitch_detector.get_frequency();
            if(f != 0.0f)
            {
                frequency_b = f;
            }
        }

        out[0][i] = sig_l;
        out[1][i] = sig_r;
    }
    load_meter.OnBlockEnd();
}

int main(void)
{
    bluemchen.Init();
    bluemchen.StartAdc();

    load_meter.Init(bluemchen.seed.AudioSampleRate(),
                    bluemchen.seed.AudioBlockSize());

    bluemchen.StartAudio(AudioCallback);

    while(1)
    {
        UpdateOled();
    }
}
