#include <daisysp.h>
#include <kxmx_bluemchen.h>
#include <q/fx/signal_conditioner.hpp>
#include <q/pitch/pitch_detector.hpp>
#include <string.h>
#include <util/CpuLoadMeter.h>
#include <algorithm>

// Namespaces...
using namespace kxmx;
using namespace daisy;
namespace q = cycfi::q;
using namespace q::literals;

// This is the default sample rate of the Daisy Seed
constexpr auto sps = 48000;

// This is the hardware housing the Daisy Seed under test
Bluemchen hardware;

// This is a handy utility class for measuring load on the MCU
CpuLoadMeter load_meter;

//Parameter instances for iterating over oscilator waveforms
Parameter wave_control_l;
Parameter wave_control_r;

// Flags
bool enable_signal_conditioning = true;
bool enable_envelope            = false;

// The dectected frequencies
float frequency_a = 0.0f;
float frequency_b = 0.0f;

// The frequency range we will be converting o v/oct (C2-C7)
float voct_fmin = 65.41f;
float voct_fmax = 2093.0f;

// The detected signal envelope
float envelope_a;
float envelope_b;

// Indexs for tracking which two strings are currently active
int idx_a = 0;
int idx_b = 1;

// These are the output oscillators, we could have use classes
// from the Q lib, but these are a little simpler to use
daisysp::Oscillator oscillator_l;
daisysp::Oscillator oscillator_r;

// This is as simple struct containting the elements necessary to process a single guitar string signal
class guitar_string
{
  public:
    guitar_string(std::string  name,
                  q::frequency lowest_frequency,
                  q::frequency highest_frequency);

    std::string               name;
    q::frequency              lowest_frequency;
    q::frequency              highest_frequency;
    q::highpass               hpf1;
    q::highpass               hpf2;
    q::lowpass                lpf1;
    q::lowpass                lpf2;
    q::fast_envelope_follower envelope;
    q::signal_conditioner     signal_conditioner;
    q::pitch_detector         pitch_detector;
};

// constructor for the above class
inline guitar_string::guitar_string(std::string  name,
                                    q::frequency lowest_frequency,
                                    q::frequency highest_frequency)
: name{name},
  highest_frequency{highest_frequency},
  lowest_frequency{lowest_frequency},
  hpf1{lowest_frequency, sps},
  hpf2{lowest_frequency, sps},
  lpf1{highest_frequency, sps},
  lpf2{highest_frequency, sps},
  signal_conditioner{q::signal_conditioner::config{},
                     lowest_frequency,
                     highest_frequency,
                     sps},
  envelope{lowest_frequency.period() * 0.6, sps},
  pitch_detector{lowest_frequency, highest_frequency, sps, -45_dB}
{
}

// Here we define all the strings we want to evaluate
std::vector<guitar_string> strings = {
    {"e", 73.4_Hz, 329.6_Hz},
    {"A", 98_Hz, 493.9_Hz},
    {"D", 146.8_Hz, 659.3_Hz},
    {"G", 185_Hz, 880.9_Hz},
    {"B", 246.9_Hz, 1108.7_Hz},
    {"E", 293.7_Hz, 1318.5_Hz},
    {"*",
     73.4_Hz,
     1318.5_Hz}, // This represents the entire spectrum of a guitar
    {"*",
     73.4_Hz,
     1318.5_Hz}, // This represents the entire spectrum of a guitar
};

/**
 * OLED display routine
 */
void UpdateOled()
{
    hardware.display.Fill(false);

    FixedCapStr<16> str(strings[idx_a].name.c_str());
    str.Append(":");
    str.AppendFloat(frequency_a);
    str.Append("Hz");
    hardware.display.SetCursor(0, 0);
    hardware.display.WriteString(str.Cstr(), Font_6x8, true);

    str.Reset(strings[idx_b].name.c_str());
    str.Append(":");
    str.AppendFloat(frequency_b);
    str.Append("Hz");
    hardware.display.SetCursor(0, 8);
    hardware.display.WriteString(str.Cstr(), Font_6x8, true);

    str.Reset("SC :");
    str.Append((enable_signal_conditioning) ? "ON" : "OFF");
    hardware.display.SetCursor(0, 16);
    hardware.display.WriteString(str.Cstr(), Font_6x8, true);

    str.Reset("CPU:");
    str.AppendFloat(load_meter.GetAvgCpuLoad() * 100.0f);
    str.Append("%");
    hardware.display.SetCursor(0, 24);
    hardware.display.WriteString(str.Cstr(), Font_6x8, true);

    hardware.display.Update();
}

/**
 * Controls update routine
 */
void UpdateControls()
{
    hardware.ProcessAllControls();

    // Handle the encoder increment to iterate through the available string configs...
    int32_t inc = hardware.encoder.Increment();
    if(inc != 0)
    {
        frequency_a = 0;
        frequency_b = 0;
        idx_a       = std::clamp(static_cast<int>(idx_a + inc),
                           static_cast<int>(0),
                           static_cast<int>(strings.size() - 2));
        idx_b       = idx_a + 1;
    }

    // Process the pots to set the wave form
    oscillator_l.SetWaveform(wave_control_l.Process());
    oscillator_r.SetWaveform(wave_control_r.Process());

    // Toggle the signal conditioning
    if(hardware.encoder.RisingEdge())
    {
        enable_signal_conditioning = !enable_signal_conditioning;
    }
}

/**
 * Audio callback routine
 */
void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // Start our load meter measurment...
    load_meter.OnBlockStart();
    // Update our controls
    UpdateControls();

    // Iterate over the samples in our block (default block size is 48)
    for(size_t i = 0; i < size; i++)
    {
        // This is the input signal
        float sig_l = in[0][i];
        float sig_r = in[1][i];

        // These will be processed signal for pitch detection
        float pd_sig_l;
        float pd_sig_r;

        // Start by cascading our high pass and low pass filters...
        pd_sig_l = strings[idx_a].hpf1(sig_l);
        pd_sig_r = strings[idx_b].hpf1(sig_r);

        pd_sig_l = strings[idx_a].hpf2(pd_sig_l);
        pd_sig_r = strings[idx_b].hpf2(pd_sig_r);

        pd_sig_l = strings[idx_a].lpf1(pd_sig_l);
        pd_sig_r = strings[idx_b].lpf1(pd_sig_r);

        pd_sig_l = strings[idx_a].lpf2(pd_sig_l);
        pd_sig_r = strings[idx_b].lpf2(pd_sig_r);

        // Now detect our envelope against the filtered signal
        envelope_a
            = std::clamp(strings[idx_a].envelope(std::abs(sig_l)), 0.0f, 1.0f);
        envelope_b
            = std::clamp(strings[idx_b].envelope(std::abs(sig_r)), 0.0f, 1.0f);

        // If signal conditioning is enabled, then apply it now
        if(enable_signal_conditioning)
        {
            pd_sig_l = strings[idx_a].signal_conditioner(pd_sig_l);
            pd_sig_r = strings[idx_b].signal_conditioner(pd_sig_r);
        }


        // send the processed sample through the pitch detector
        // of the currently selected sting
        if(strings[idx_a].pitch_detector(pd_sig_l))
        {
            // The pitch detector has a result to report, update frequency_a.
            auto f = strings[idx_a].pitch_detector.get_frequency();
            if(f != 0.0f)
            {
                frequency_a = f;
            }
        }

        if(strings[idx_b].pitch_detector(pd_sig_r))
        {
            // The pitch detector has a result to report, update frequency_b.
            auto f = strings[idx_b].pitch_detector.get_frequency();
            if(f != 0.0f)
            {
                frequency_b = f;
            }
        }


        out[0][i] = oscillator_l.Process();
        out[1][i] = oscillator_r.Process();

        // out[0][i] = sig_l;
        // out[1][i] = sig_r;
    }

    // If enabled, apply the envelope to the oscilators
    if(enable_envelope)
    {
        oscillator_l.SetAmp(envelope_a);
        oscillator_r.SetAmp(envelope_b);
    }

    // Set the frequencies of the oscillators...
    oscillator_l.SetFreq(frequency_a);
    oscillator_r.SetFreq(frequency_b);

    // Calculate our 12 bit v/oct DAC output
    float voct = std::clamp(((1.0f / log10f(voct_fmax / voct_fmin))
                             * log10f(frequency_a / voct_fmin))
                                * 4095.0f,
                            0.0f,
                            4095.0f);

    // Write the v/oct and envelope signals to the two DAC channels...
    hardware.seed.dac.WriteValue(daisy::DacHandle::Channel::ONE, voct);
    hardware.seed.dac.WriteValue(daisy::DacHandle::Channel::TWO,
                                 envelope_a * 4095.0f);

    // wrap up the load meter calculation
    load_meter.OnBlockEnd();
}

int main(void)
{
    // Initialize the hardware
    hardware.Init();
    hardware.StartAdc();

    load_meter.Init(hardware.seed.AudioSampleRate(),
                    hardware.seed.AudioBlockSize());


    // Init the two oscillators
    oscillator_l.Init(hardware.seed.AudioSampleRate());
    oscillator_l.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_TRI);
    oscillator_l.SetAmp(0.5f);

    oscillator_r.Init(hardware.seed.AudioSampleRate());
    oscillator_r.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    oscillator_r.SetAmp(0.5f);

    // Init the waveform control pots
    int num_waves = daisysp::Oscillator::WAVE_LAST - 1;
    wave_control_l.Init(
        hardware.controls[hardware.CTRL_1], 0.0, num_waves, Parameter::LINEAR);
    wave_control_r.Init(
        hardware.controls[hardware.CTRL_2], 0.0, num_waves, Parameter::LINEAR);

    // Start the audio...
    hardware.StartAudio(AudioCallback);


    while(1)
    {
        // Update the Whenever the AudioCallback is not runnning.
        UpdateOled();
    }
}
