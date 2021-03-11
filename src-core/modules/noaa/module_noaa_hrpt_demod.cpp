#include "module_noaa_hrpt_demod.h"
#include <dsp/fir_gen.h>
#include "logger.h"
#include "imgui/imgui.h"
#include <volk/volk.h>

#define M_PI 3.14159265358979323846

// Return filesize
size_t getFilesize(std::string filepath);

namespace noaa
{
    NOAAHRPTDemodModule::NOAAHRPTDemodModule(std::string input_file, std::string output_file_hint, std::map<std::string, std::string> parameters) : ProcessingModule(input_file, output_file_hint, parameters),
                                                                                                                                                    d_samplerate(std::stoi(parameters["samplerate"])),
                                                                                                                                                    d_buffer_size(std::stoi(parameters["buffer_size"]))
    {
        if (parameters["baseband_format"] == "i16")
        {
            i16 = true;
        }
        else if (parameters["baseband_format"] == "i8")
        {
            i8 = true;
        }
        else if (parameters["baseband_format"] == "f32")
        {
            f32 = true;
        }
        else if (parameters["baseband_format"] == "w8")
        {
            w8 = true;
        }

        // Init DSP blocks
        agc = std::make_shared<libdsp::AgcCC>(0.002e-3f, 1.0f, 0.5f / 32768.0f, 65536);
        pll = std::make_shared<libdsp::BPSKCarrierPLL>(0.01f, powf(0.01f, 2) / 4.0f, (3.0f * M_PI * 100e3f) / (float)d_samplerate);
        rrc = std::make_shared<dsp::FIRFilterFFF>(1, libdsp::firgen::root_raised_cosine(1, (float)d_samplerate / 2.0f, 665400.0f, 0.5f, 31));
        rec = std::make_shared<dsp::ClockRecoveryMMFF>(((float)d_samplerate / (float)665400) / 2.0f, powf(0.01, 2) / 4.0f, 0.5f, 0.01f, 100e-6f);
        def = std::make_shared<NOAADeframer>();

        // Buffers
        in_buffer = new std::complex<float>[d_buffer_size];
        in_buffer2 = new std::complex<float>[d_buffer_size];
        agc_buffer = new std::complex<float>[d_buffer_size];
        agc_buffer2 = new std::complex<float>[d_buffer_size];
        pll_buffer = new float[d_buffer_size];
        pll_buffer2 = new float[d_buffer_size];
        rrc_buffer = new float[d_buffer_size];
        rrc_buffer2 = new float[d_buffer_size];
        rec_buffer = new float[d_buffer_size];
        rec_buffer2 = new float[d_buffer_size];
        bits_buffer = new uint8_t[d_buffer_size * 10];

        buffer_i16 = new int16_t[d_buffer_size * 2];
        buffer_i8 = new int8_t[d_buffer_size * 2];
        buffer_u8 = new uint8_t[d_buffer_size * 2];

        // Init FIFOs
        in_pipe = std::make_shared<RingBuffer<std::complex<float>>>(d_buffer_size);
        agc_pipe = std::make_shared<RingBuffer<std::complex<float>>>(d_buffer_size);
        pll_pipe = std::make_shared<RingBuffer<float>>(d_buffer_size);
        rrc_pipe = std::make_shared<RingBuffer<float>>(d_buffer_size);
        rec_pipe = std::make_shared<RingBuffer<float>>(d_buffer_size);
    }

    std::vector<ModuleDataType> NOAAHRPTDemodModule::getInputTypes()
    {
        return {DATA_FILE, DATA_STREAM};
    }

    std::vector<ModuleDataType> NOAAHRPTDemodModule::getOutputTypes()
    {
        return {DATA_FILE};
    }

    NOAAHRPTDemodModule::~NOAAHRPTDemodModule()
    {
        delete[] in_buffer;
        delete[] in_buffer2;
        delete[] agc_buffer;
        delete[] agc_buffer2;
        delete[] rrc_buffer;
        delete[] rrc_buffer2;
        delete[] pll_buffer;
        delete[] pll_buffer2;
        delete[] rec_buffer;
        delete[] rec_buffer2;
        delete[] bits_buffer;
        delete[] buffer_i16;
        delete[] buffer_i8;
        delete[] buffer_u8;
        //delete[] in_pipe;
        //delete[] rrc_pipe;
        //delete[] agc_pipe;
        //delete[] pll_pipe;
        //delete[] rec_pipe;
    }

    void NOAAHRPTDemodModule::process()
    {
        if (input_data_type == DATA_FILE)
            filesize = getFilesize(d_input_file);
        else
            filesize = 0;

        if (input_data_type == DATA_FILE)
            data_in = std::ifstream(d_input_file, std::ios::binary);

        data_out = std::ofstream(d_output_file_hint + ".raw16", std::ios::binary);
        d_output_files.push_back(d_output_file_hint + ".raw16");

        logger->info("Using input baseband " + d_input_file);
        logger->info("Demodulating to " + d_output_file_hint + ".raw16");
        logger->info("Buffer size : " + std::to_string(d_buffer_size));

        time_t lastTime = 0;

        agcRun = rrcRun = pllRun = recRun = true;

        fileThread = std::thread(&NOAAHRPTDemodModule::fileThreadFunction, this);
        agcThread = std::thread(&NOAAHRPTDemodModule::agcThreadFunction, this);
        pllThread = std::thread(&NOAAHRPTDemodModule::pllThreadFunction, this);
        rrcThread = std::thread(&NOAAHRPTDemodModule::rrcThreadFunction, this);
        recThread = std::thread(&NOAAHRPTDemodModule::clockrecoveryThreadFunction, this);

        int dat_size = 0;
        while (input_data_type == DATA_STREAM ? input_active.load() : !data_in.eof())
        {
            dat_size = rec_pipe->read(rec_buffer2, d_buffer_size);

            if (dat_size <= 0)
                continue;

            volk_32f_binary_slicer_8i((int8_t *)bits_buffer, rec_buffer2, dat_size);

            std::vector<uint16_t> frames = def->work(bits_buffer, dat_size);

            // Count frames
            frame_count += frames.size();

            // Write to file
            if (frames.size() > 0)
                data_out.write((char *)&frames[0], frames.size() * sizeof(uint16_t));

            if (time(NULL) % 10 == 0 && lastTime != time(NULL))
            {
                lastTime = time(NULL);
                logger->info("Progress " + std::to_string(round(((float)progress / (float)filesize) * 1000.0f) / 10.0f) + "%, Frames : " + std::to_string(frame_count / 11090));
            }
        }

        logger->info("Demodulation finished");

        if (fileThread.joinable())
            fileThread.join();

        logger->debug("FILE OK");
    }

    void NOAAHRPTDemodModule::fileThreadFunction()
    {
        int gotten;
        while (input_data_type == DATA_STREAM ? input_active.load() : !data_in.eof())
        {
            // Get baseband, possibly convert to F32
            if (f32)
            {
                if (input_data_type == DATA_FILE)
                    data_in.read((char *)in_buffer, d_buffer_size * sizeof(std::complex<float>));
                else
                    gotten = input_fifo->pop((uint8_t *)in_buffer, d_buffer_size, sizeof(std::complex<float>));
            }
            else if (i16)
            {
                if (input_data_type == DATA_FILE)
                    data_in.read((char *)buffer_i16, d_buffer_size * sizeof(int16_t) * 2);
                else
                    gotten = input_fifo->pop((uint8_t *)buffer_i16, d_buffer_size, sizeof(int16_t) * 2);

                for (int i = 0; i < d_buffer_size; i++)
                {
                    using namespace std::complex_literals;
                    in_buffer[i] = (float)buffer_i16[i * 2] + (float)buffer_i16[i * 2 + 1] * 1if;
                }
            }
            else if (i8)
            {
                if (input_data_type == DATA_FILE)
                    data_in.read((char *)buffer_i8, d_buffer_size * sizeof(int8_t) * 2);
                else
                    gotten = input_fifo->pop((uint8_t *)buffer_i8, d_buffer_size, sizeof(int8_t) * 2);

                for (int i = 0; i < d_buffer_size; i++)
                {
                    using namespace std::complex_literals;
                    in_buffer[i] = (float)buffer_i8[i * 2] + (float)buffer_i8[i * 2 + 1] * 1if;
                }
            }
            else if (w8)
            {
                if (input_data_type == DATA_FILE)
                    data_in.read((char *)buffer_u8, d_buffer_size * sizeof(uint8_t) * 2);
                else
                    gotten = input_fifo->pop((uint8_t *)buffer_i8, d_buffer_size, sizeof(uint8_t) * 2);

                for (int i = 0; i < d_buffer_size; i++)
                {
                    float imag = (buffer_u8[i * 2] - 127) * 0.004f;
                    float real = (buffer_u8[i * 2 + 1] - 127) * 0.004f;
                    using namespace std::complex_literals;
                    in_buffer[i] = real + imag * 1if;
                }
            }

            if (input_data_type == DATA_FILE)
                progress = data_in.tellg();
            else
                progress = 0;

            progress = data_in.tellg();

            in_pipe->write(in_buffer, d_buffer_size);
        }

        if (input_data_type == DATA_FILE)
            data_in.close();

        // Exit all threads... Without causing a race condition!
        agcRun = rrcRun = pllRun = recRun = false;

        in_pipe->stopWriter();
        in_pipe->stopReader();

        agc_pipe->stopWriter();

        if (agcThread.joinable())
            agcThread.join();

        logger->debug("AGC OK");

        agc_pipe->stopReader();
        pll_pipe->stopWriter();

        if (pllThread.joinable())
            pllThread.join();

        logger->debug("PLL OK");

        pll_pipe->stopReader();
        rrc_pipe->stopWriter();

        if (rrcThread.joinable())
            rrcThread.join();

        logger->debug("RRC OK");

        rrc_pipe->stopReader();
        rec_pipe->stopWriter();

        if (recThread.joinable())
            recThread.join();

        logger->debug("REC OK");

        data_out.close();

        rec_pipe->stopReader();
    }

    void NOAAHRPTDemodModule::agcThreadFunction()
    {
        int gotten;
        while (agcRun)
        {
            gotten = in_pipe->read(in_buffer2, d_buffer_size);

            if (gotten <= 0)
                continue;

            /// AGC
            agc->work(in_buffer2, gotten, agc_buffer);

            agc_pipe->write(agc_buffer, gotten);
        }
    }

    void NOAAHRPTDemodModule::pllThreadFunction()
    {
        int gotten;
        while (pllRun)
        {
            gotten = agc_pipe->read(agc_buffer2, d_buffer_size);

            if (gotten <= 0)
                continue;

            // Costas loop, frequency offset recovery
            pll->work(agc_buffer2, gotten, pll_buffer);

            pll_pipe->write(pll_buffer, gotten);
        }
    }

    void NOAAHRPTDemodModule::rrcThreadFunction()
    {
        int gotten;
        while (rrcRun)
        {
            gotten = pll_pipe->read(pll_buffer2, d_buffer_size);

            if (gotten <= 0)
                continue;

            // Root-raised-cosine filtering
            int out = rrc->work(pll_buffer2, gotten, rrc_buffer);

            rrc_pipe->write(rrc_buffer, out);
        }
    }

    void NOAAHRPTDemodModule::clockrecoveryThreadFunction()
    {
        int gotten;
        while (recRun)
        {
            gotten = rrc_pipe->read(rrc_buffer2, d_buffer_size);

            if (gotten <= 0)
                continue;

            int recovered_size = 0;

            try
            {
                // Clock recovery
                recovered_size = rec->work(rrc_buffer2, gotten, rec_buffer);
            }
            catch (std::runtime_error &e)
            {
                logger->error(e.what());
            }

            rec_pipe->write(rec_buffer, recovered_size);
        }
    }

    void NOAAHRPTDemodModule::drawUI()
    {
        ImGui::Begin("NOAA HRPT Demodulator", NULL, NOWINDOW_FLAGS);

        ImGui::BeginGroup();
        // Constellation
        {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(ImGui::GetCursorScreenPos(),
                                     ImVec2(ImGui::GetCursorScreenPos().x + 200, ImGui::GetCursorScreenPos().y + 200),
                                     ImColor::HSV(0, 0, 0));

            for (int i = 0; i < 2048; i++)
            {
                draw_list->AddCircleFilled(ImVec2(ImGui::GetCursorScreenPos().x + (int)(100 + rec_buffer2[i] * 90) % 200,
                                                  ImGui::GetCursorScreenPos().y + (int)(100 + rng.gasdev() * 15) % 200),
                                           2,
                                           ImColor::HSV(113.0 / 360.0, 1, 1, 1.0));
            }

            ImGui::Dummy(ImVec2(200 + 3, 200 + 3));
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        {
            ImGui::Button("Deframer", {200, 20});
            {
                ImGui::Text("Frames : ");

                ImGui::SameLine();

                ImGui::TextColored(ImColor::HSV(113.0 / 360.0, 1, 1, 1.0), std::to_string(frame_count / 11090).c_str());
            }
        }
        ImGui::EndGroup();

        ImGui::ProgressBar((float)progress / (float)filesize, ImVec2(ImGui::GetWindowWidth() - 10, 20));

        ImGui::End();
    }

    std::string NOAAHRPTDemodModule::getID()
    {
        return "noaa_hrpt_demod";
    }

    std::vector<std::string> NOAAHRPTDemodModule::getParameters()
    {
        return {"samplerate", "buffer_size", "baseband_format"};
    }

    std::shared_ptr<ProcessingModule> NOAAHRPTDemodModule::getInstance(std::string input_file, std::string output_file_hint, std::map<std::string, std::string> parameters)
    {
        return std::make_shared<NOAAHRPTDemodModule>(input_file, output_file_hint, parameters);
    }

    std::vector<uint8_t> NOAAHRPTDemodModule::getBytes(uint8_t *bits, int length)
    {
        std::vector<uint8_t> bytesToRet;
        for (int ii = 0; ii < length; ii++)
        {
            byteToWrite = (byteToWrite << 1) | bits[ii];
            inByteToWrite++;

            if (inByteToWrite == 8)
            {
                bytesToRet.push_back(byteToWrite);
                inByteToWrite = 0;
            }
        }

        return bytesToRet;
    }
} // namespace noaa