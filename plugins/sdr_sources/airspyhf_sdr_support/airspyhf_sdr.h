#pragma once

#include "common/dsp_source_sink/dsp_sample_source.h"
#ifdef __ANDROID__
#include "airspyhf.h"
#else
#include <libairspyhf/airspyhf.h>
#endif
#include "logger.h"
#include "common/rimgui.h"
#include "common/widgets/double_list.h"

class AirspyHFSource : public dsp::DSPSampleSource
{
protected:
    bool is_open = false, is_started = false;
    airspyhf_device *airspyhf_dev_obj;
    static int _rx_callback(airspyhf_transfer_t *t);

    widgets::DoubleList samplerate_widget;

    int agc_mode = 0;
    int attenuation = 0;
    bool hf_lna_enabled = false;

    void set_atte();
    void set_lna();
    void set_agcs();

    void open_sdr();

public:
    AirspyHFSource(dsp::SourceDescriptor source) : DSPSampleSource(source), samplerate_widget("Samplerate")
    {
    }

    ~AirspyHFSource()
    {
        stop();
        close();
    }

    void set_settings(nlohmann::json settings);
    nlohmann::json get_settings();

    void open();
    void start();
    void stop();
    void close();

    void set_frequency(uint64_t frequency);

    void drawControlUI();

    void set_samplerate(uint64_t samplerate);
    uint64_t get_samplerate();

    static std::string getID() { return "airspyhf"; }
    static std::shared_ptr<dsp::DSPSampleSource> getInstance(dsp::SourceDescriptor source) { return std::make_shared<AirspyHFSource>(source); }
    static std::vector<dsp::SourceDescriptor> getAvailableSources();
};