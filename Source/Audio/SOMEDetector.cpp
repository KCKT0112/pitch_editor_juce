#include "SOMEDetector.h"
#include <cmath>
#include <algorithm>
#include <numeric>

SOMEDetector::SOMEDetector() = default;
SOMEDetector::~SOMEDetector() = default;

bool SOMEDetector::loadModel(const juce::File& modelPath)
{
#ifdef HAVE_ONNXRUNTIME
    try
    {
        onnxEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SOMEDetector");

        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(4);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        std::wstring modelPathW = modelPath.getFullPathName().toWideCharPointer();
        onnxSession = std::make_unique<Ort::Session>(*onnxEnv, modelPathW.c_str(), sessionOptions);
#else
        std::string modelPathStr = modelPath.getFullPathName().toStdString();
        onnxSession = std::make_unique<Ort::Session>(*onnxEnv, modelPathStr.c_str(), sessionOptions);
#endif

        Ort::AllocatorWithDefaultOptions allocator;

        inputNameStrings.clear();
        outputNameStrings.clear();
        inputNames.clear();
        outputNames.clear();

        size_t numInputs = onnxSession->GetInputCount();
        size_t numOutputs = onnxSession->GetOutputCount();

        for (size_t i = 0; i < numInputs; ++i)
        {
            auto namePtr = onnxSession->GetInputNameAllocated(i, allocator);
            inputNameStrings.push_back(namePtr.get());
        }

        for (size_t i = 0; i < numOutputs; ++i)
        {
            auto namePtr = onnxSession->GetOutputNameAllocated(i, allocator);
            outputNameStrings.push_back(namePtr.get());
        }

        for (const auto& name : inputNameStrings)
            inputNames.push_back(name.c_str());
        for (const auto& name : outputNameStrings)
            outputNames.push_back(name.c_str());

        loaded = true;
        DBG("SOME model loaded: " << inputNameStrings.size() << " inputs, " << outputNameStrings.size() << " outputs");
        return true;
    }
    catch (const Ort::Exception& e)
    {
        DBG("ONNX Runtime error: " << e.what());
        loaded = false;
        return false;
    }
#else
    return false;
#endif
}

std::vector<float> SOMEDetector::resampleTo44k(const float* audio, int numSamples, int srcRate)
{
    if (srcRate == SAMPLE_RATE)
        return std::vector<float>(audio, audio + numSamples);

    double ratio = static_cast<double>(SAMPLE_RATE) / srcRate;
    int outSamples = static_cast<int>(numSamples * ratio);
    std::vector<float> resampled(outSamples);

    for (int i = 0; i < outSamples; ++i)
    {
        double srcPos = i / ratio;
        int srcIdx = static_cast<int>(srcPos);
        double frac = srcPos - srcIdx;

        if (srcIdx + 1 < numSamples)
            resampled[i] = static_cast<float>(audio[srcIdx] * (1.0 - frac) + audio[srcIdx + 1] * frac);
        else if (srcIdx < numSamples)
            resampled[i] = audio[srcIdx];
    }
    return resampled;
}

// RMS calculation for slicer
std::vector<double> SOMEDetector::getRms(const std::vector<float>& samples, int frameLength, int hopLength)
{
    std::vector<double> output;
    size_t outputSize = samples.size() / hopLength;
    output.reserve(outputSize);

    for (size_t i = 0; i < outputSize; ++i)
    {
        size_t halfFrame = static_cast<size_t>(frameLength / 2);
        size_t center = i * hopLength;
        size_t start = (center < halfFrame) ? 0 : (center - halfFrame);
        size_t end = std::min(samples.size(), center + halfFrame);

        double sum = 0.0;
        for (size_t j = start; j < end; ++j)
            sum += samples[j] * samples[j];
        output.push_back(std::sqrt(sum / frameLength));
    }
    return output;
}

// Audio slicer based on silence detection
SOMEDetector::MarkerList SOMEDetector::sliceAudio(const std::vector<float>& samples) const
{
    constexpr float threshold = 0.02f;
    constexpr int hopSize = 441;
    constexpr int winSize = 441 * 4;
    constexpr int minLength = 500;
    constexpr int minInterval = 30;
    constexpr int maxSilKept = 50;

    size_t minFrames = static_cast<size_t>(minLength);
    if ((samples.size() + hopSize - 1) / hopSize <= minFrames)
        return {{0, static_cast<int64_t>(samples.size())}};

    auto rmsList = getRms(samples, winSize, hopSize);
    MarkerList silTags;
    int64_t silenceStart = -1;
    int64_t clipStart = 0;

    auto argmin = [](const std::vector<double>& v, size_t begin, size_t end) -> int64_t {
        if (begin >= end || end > v.size()) return 0;
        return static_cast<int64_t>(std::distance(v.begin() + begin,
            std::min_element(v.begin() + begin, v.begin() + end)));
    };

    for (size_t i = 0; i < rmsList.size(); ++i)
    {
        if (rmsList[i] < threshold)
        {
            if (silenceStart < 0)
                silenceStart = static_cast<int64_t>(i);
            continue;
        }

        if (silenceStart < 0)
            continue;

        int64_t ii = static_cast<int64_t>(i);
        bool isLeadingSilence = silenceStart == 0 && ii > maxSilKept;
        bool needSlice = ii - silenceStart >= minInterval && ii - clipStart >= minLength;

        if (!isLeadingSilence && !needSlice)
        {
            silenceStart = -1;
            continue;
        }

        if (ii - silenceStart <= maxSilKept)
        {
            int64_t pos = argmin(rmsList, silenceStart, i + 1) + silenceStart;
            silTags.emplace_back(silenceStart == 0 ? 0 : pos, pos);
            clipStart = pos;
        }
        else
        {
            int64_t posL = argmin(rmsList, silenceStart, silenceStart + maxSilKept + 1) + silenceStart;
            int64_t posR = argmin(rmsList, i - maxSilKept, i + 1) + ii - maxSilKept;
            silTags.emplace_back(silenceStart == 0 ? 0 : posL, posR);
            clipStart = posR;
        }
        silenceStart = -1;
    }

    if (silenceStart >= 0 && static_cast<int64_t>(rmsList.size()) - silenceStart >= minInterval)
    {
        int64_t silenceEnd = std::min(static_cast<int64_t>(rmsList.size() - 1), silenceStart + maxSilKept);
        int64_t pos = argmin(rmsList, silenceStart, silenceEnd + 1) + silenceStart;
        silTags.emplace_back(pos, static_cast<int64_t>(rmsList.size() + 1));
    }

    if (silTags.empty())
        return {{0, static_cast<int64_t>(samples.size())}};

    MarkerList chunks;
    if (silTags[0].first > 0)
        chunks.emplace_back(0, silTags[0].first * hopSize);

    for (size_t i = 0; i < silTags.size() - 1; ++i)
        chunks.emplace_back(silTags[i].second * hopSize, silTags[i + 1].first * hopSize);

    if (silTags.back().second < static_cast<int64_t>(rmsList.size()))
        chunks.emplace_back(silTags.back().second * hopSize, static_cast<int64_t>(rmsList.size() * hopSize));

    return chunks;
}

bool SOMEDetector::inferChunk(const std::vector<float>& chunk, std::vector<float>& midi,
                               std::vector<bool>& rest, std::vector<float>& dur)
{
#ifdef HAVE_ONNXRUNTIME
    if (!onnxSession)
        return false;

    try
    {
        std::vector<int64_t> shape = {1, static_cast<int64_t>(chunk.size())};
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<float> chunkCopy = chunk;
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, chunkCopy.data(), chunkCopy.size(), shape.data(), shape.size());

        std::vector<Ort::Value> inputTensors;
        inputTensors.push_back(std::move(inputTensor));

        auto outputs = onnxSession->Run(Ort::RunOptions{nullptr},
            inputNames.data(), inputTensors.data(), inputTensors.size(),
            outputNames.data(), outputNames.size());

        float* midiData = outputs[0].GetTensorMutableData<float>();
        bool* restData = outputs[1].GetTensorMutableData<bool>();
        float* durData = outputs[2].GetTensorMutableData<float>();

        size_t count = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
        midi.assign(midiData, midiData + count);
        rest.assign(restData, restData + count);
        dur.assign(durData, durData + count);

        return true;
    }
    catch (const Ort::Exception& e)
    {
        DBG("SOME chunk inference error: " << e.what());
        return false;
    }
#else
    return false;
#endif
}

std::vector<SOMEDetector::NoteEvent> SOMEDetector::detectNotes(
    const float* audio, int numSamples, int sampleRate)
{
    return detectNotesWithProgress(audio, numSamples, sampleRate, nullptr);
}

std::vector<SOMEDetector::NoteEvent> SOMEDetector::detectNotesWithProgress(
    const float* audio, int numSamples, int sampleRate,
    std::function<void(double)> progressCallback)
{
#ifdef HAVE_ONNXRUNTIME
    if (!loaded || !onnxSession)
    {
        DBG("SOME model not loaded");
        return {};
    }

    if (progressCallback) progressCallback(0.05);

    std::vector<float> waveform = resampleTo44k(audio, numSamples, sampleRate);
    int64_t totalSize = static_cast<int64_t>(waveform.size());

    if (progressCallback) progressCallback(0.1);

    MarkerList chunks = sliceAudio(waveform);
    DBG("SOME: sliced into " << chunks.size() << " chunks");

    if (chunks.empty())
        return {};

    // Calculate total frames for progress
    int64_t totalFrames = 0;
    for (const auto& [start, end] : chunks)
        totalFrames += (end - start);

    std::vector<NoteEvent> allNotes;
    int64_t processedFrames = 0;

    // Process chunks sequentially (like dataset-tools)
    for (const auto& [beginFrame, endFrame] : chunks)
    {
        if (endFrame <= beginFrame || beginFrame >= totalSize)
            continue;

        int64_t actualEnd = std::min(endFrame, totalSize);
        std::vector<float> chunkData(waveform.begin() + beginFrame, waveform.begin() + actualEnd);

        std::vector<float> noteMidi;
        std::vector<bool> noteRest;
        std::vector<float> noteDur;

        if (!inferChunk(chunkData, noteMidi, noteRest, noteDur))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "SOME Error", "Inference failed");
            return {};
        }

        if (noteMidi.empty())
            continue;

        // Calculate start frame for this chunk
        // Use max of chunk start position and last note end position (like dataset-tools)
        int chunkStartFrame = static_cast<int>(beginFrame / HOP_SIZE);
        if (!allNotes.empty())
            chunkStartFrame = std::max(chunkStartFrame, allNotes.back().endFrame);

        // Build notes from this chunk
        // Duration is in seconds, convert to frames
        int currentFrame = chunkStartFrame;

        for (size_t i = 0; i < noteMidi.size(); ++i)
        {
            // Duration in seconds -> frames
            int durationFrames = static_cast<int>(std::round(noteDur[i] * SAMPLE_RATE / HOP_SIZE));
            if (durationFrames < 1) durationFrames = 1;

            int endFrameNote = currentFrame + durationFrames;

            if (!noteRest[i])
            {
                NoteEvent event;
                event.startFrame = currentFrame;
                event.endFrame = endFrameNote;
                event.midiNote = noteMidi[i];
                event.isRest = false;
                allNotes.push_back(event);
            }

            currentFrame = endFrameNote;
        }

        processedFrames += (actualEnd - beginFrame);
        if (progressCallback)
            progressCallback(0.1 + 0.85 * static_cast<double>(processedFrames) / totalFrames);
    }

    if (progressCallback) progressCallback(1.0);

    DBG("SOME: detected " << allNotes.size() << " notes total");
    return allNotes;

#else
    return {};
#endif
}

void SOMEDetector::detectNotesStreaming(
    const float* audio, int numSamples, int sampleRate,
    std::function<void(const std::vector<NoteEvent>&)> noteCallback,
    std::function<void(double)> progressCallback)
{
#ifdef HAVE_ONNXRUNTIME
    if (!loaded || !onnxSession)
    {
        DBG("SOME model not loaded");
        return;
    }

    if (progressCallback) progressCallback(0.05);

    std::vector<float> waveform = resampleTo44k(audio, numSamples, sampleRate);
    int64_t totalSize = static_cast<int64_t>(waveform.size());

    if (progressCallback) progressCallback(0.1);

    MarkerList chunks = sliceAudio(waveform);
    DBG("SOME streaming: sliced into " << chunks.size() << " chunks");

    if (chunks.empty())
        return;

    int64_t totalFrames = 0;
    for (const auto& [start, end] : chunks)
        totalFrames += (end - start);

    int lastEndFrame = 0;
    int64_t processedFrames = 0;

    for (const auto& [beginFrame, endFrame] : chunks)
    {
        if (endFrame <= beginFrame || beginFrame >= totalSize)
            continue;

        int64_t actualEnd = std::min(endFrame, totalSize);
        std::vector<float> chunkData(waveform.begin() + beginFrame, waveform.begin() + actualEnd);

        std::vector<float> noteMidi;
        std::vector<bool> noteRest;
        std::vector<float> noteDur;

        if (!inferChunk(chunkData, noteMidi, noteRest, noteDur))
        {
            DBG("SOME chunk inference failed");
            continue;
        }

        if (noteMidi.empty())
            continue;

        int chunkStartFrame = static_cast<int>(beginFrame / HOP_SIZE);
        chunkStartFrame = std::max(chunkStartFrame, lastEndFrame);

        std::vector<NoteEvent> chunkNotes;
        int currentFrame = chunkStartFrame;

        for (size_t i = 0; i < noteMidi.size(); ++i)
        {
            int durationFrames = static_cast<int>(std::round(noteDur[i] * SAMPLE_RATE / HOP_SIZE));
            if (durationFrames < 1) durationFrames = 1;

            int endFrameNote = currentFrame + durationFrames;

            if (!noteRest[i])
            {
                NoteEvent event;
                event.startFrame = currentFrame;
                event.endFrame = endFrameNote;
                event.midiNote = noteMidi[i];
                event.isRest = false;
                chunkNotes.push_back(event);
            }

            currentFrame = endFrameNote;
        }

        lastEndFrame = currentFrame;

        // Immediately callback with this chunk's notes
        if (noteCallback && !chunkNotes.empty())
            noteCallback(chunkNotes);

        processedFrames += (actualEnd - beginFrame);
        if (progressCallback)
            progressCallback(0.1 + 0.85 * static_cast<double>(processedFrames) / totalFrames);
    }

    if (progressCallback) progressCallback(1.0);
#endif
}
