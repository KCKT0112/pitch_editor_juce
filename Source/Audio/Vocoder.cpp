#include "Vocoder.h"
#include "../Utils/Constants.h"
#include "../Utils/PlatformPaths.h"
#include <cmath>
#include <thread>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

Vocoder::Vocoder()
{
    // Open log file in platform-appropriate logs directory
    auto logPath = PlatformPaths::getLogFile("vocoder_log.txt");
    logFile = std::make_unique<std::ofstream>(logPath.getFullPathName().toStdString(), std::ios::app);
    
    if (logFile && logFile->is_open())
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        *logFile << "\n========== Vocoder Session Started at " << std::ctime(&time) << " ==========\n";
        logFile->flush();
    }
    
#ifdef HAVE_ONNXRUNTIME
    // Initialize ONNX Runtime environment
    try {
        onnxEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "PitchEditor");
        allocator = std::make_unique<Ort::AllocatorWithDefaultOptions>();
        log("ONNX Runtime initialized successfully");
    } catch (const Ort::Exception& e) {
        log("Failed to initialize ONNX Runtime: " + std::string(e.what()));
    }
#endif
}

Vocoder::~Vocoder()
{
#ifdef HAVE_ONNXRUNTIME
    onnxSession.reset();
    onnxEnv.reset();
#endif
    if (logFile && logFile->is_open())
    {
        log("Vocoder session ended");
        logFile->close();
    }
}

void Vocoder::log(const std::string& message)
{
    DBG(message);
    if (logFile && logFile->is_open())
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif
        
        *logFile << std::put_time(&tm_buf, "%H:%M:%S") << "." 
                 << std::setfill('0') << std::setw(3) << ms.count() 
                 << " | " << message << "\n";
        logFile->flush();
    }
}

bool Vocoder::isOnnxRuntimeAvailable()
{
#ifdef HAVE_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

bool Vocoder::loadModel(const juce::File& modelPath)
{
#ifdef HAVE_ONNXRUNTIME
    if (!onnxEnv)
    {
        log("ONNX Runtime not initialized");
        return false;
    }
    
    if (!modelPath.existsAsFile())
    {
        log("Vocoder: Model file not found: " + modelPath.getFullPathName().toStdString());
        return false;
    }
    
    try {
        // Create session with current settings
        Ort::SessionOptions sessionOptions = createSessionOptions();
        
        // Create session
#ifdef _WIN32
        std::wstring modelPathW = modelPath.getFullPathName().toWideCharPointer();
        onnxSession = std::make_unique<Ort::Session>(*onnxEnv, modelPathW.c_str(), sessionOptions);
#else
        std::string modelPathStr = modelPath.getFullPathName().toStdString();
        onnxSession = std::make_unique<Ort::Session>(*onnxEnv, modelPathStr.c_str(), sessionOptions);
#endif
        
        // Get input names
        size_t numInputs = onnxSession->GetInputCount();
        inputNameStrings.clear();
        inputNames.clear();
        
        for (size_t i = 0; i < numInputs; ++i)
        {
            auto namePtr = onnxSession->GetInputNameAllocated(i, *allocator);
            inputNameStrings.push_back(namePtr.get());
        }
        for (auto& name : inputNameStrings)
        {
            inputNames.push_back(name.c_str());
        }
        
        // Get output names
        size_t numOutputs = onnxSession->GetOutputCount();
        outputNameStrings.clear();
        outputNames.clear();
        
        for (size_t i = 0; i < numOutputs; ++i)
        {
            auto namePtr = onnxSession->GetOutputNameAllocated(i, *allocator);
            outputNameStrings.push_back(namePtr.get());
        }
        for (auto& name : outputNameStrings)
        {
            outputNames.push_back(name.c_str());
        }
        
        log("Vocoder: ONNX model loaded successfully");
        log("  Input names: " + std::string(inputNames.size() > 0 ? inputNames[0] : "none"));
        log("  Output names: " + std::string(outputNames.size() > 0 ? outputNames[0] : "none"));
        
        modelFile = modelPath;
        loaded = true;
        return true;
        
    } catch (const Ort::Exception& e) {
        log("Failed to load ONNX model: " + std::string(e.what()));
        loaded = false;
        return false;
    }
#else
    // Without ONNX Runtime, try to load config from same directory
    auto configPath = modelPath.getParentDirectory().getChildFile("config.json");
    if (configPath.existsAsFile())
    {
        auto configText = configPath.loadFileAsString();
        auto config = juce::JSON::parse(configText);
        
        if (config.isObject())
        {
            auto configObj = config.getDynamicObject();
            if (configObj)
            {
                sampleRate = configObj->getProperty("sampling_rate");
                hopSize = configObj->getProperty("hop_size");
                numMels = configObj->getProperty("num_mels");
                pitchControllable = configObj->getProperty("pc_aug");
            }
        }
    }
    
    log("Vocoder: ONNX Runtime not available, using sine fallback");
    loaded = true;  // Allow "loaded" state for fallback
    return true;
#endif
}

std::vector<float> Vocoder::infer(const std::vector<std::vector<float>>& mel,
                                   const std::vector<float>& f0)
{
    if (!loaded || mel.empty() || f0.empty())
        return {};
    
    size_t numFrames = std::min(mel.size(), f0.size());
    
    log("Starting inference with " + std::to_string(numFrames) + " frames");
    
    auto startTotal = std::chrono::high_resolution_clock::now();
    
#ifdef HAVE_ONNXRUNTIME
    if (!onnxSession)
    {
        log("ONNX session not available, using fallback");
        return generateSineFallback(f0);
    }
    
    try {
        auto startPrep = std::chrono::high_resolution_clock::now();
        
        // Prepare mel input: [batch=1, num_mels, frames]
        std::vector<int64_t> melShape = {1, static_cast<int64_t>(numMels), static_cast<int64_t>(numFrames)};
        std::vector<float> melData(numMels * numFrames);
        
        // Transpose mel from [T, num_mels] to [num_mels, T]
        for (size_t frame = 0; frame < numFrames; ++frame)
        {
            for (int m = 0; m < numMels && m < static_cast<int>(mel[frame].size()); ++m)
            {
                melData[m * numFrames + frame] = mel[frame][m];
            }
        }
        
        // Log mel statistics
        float melMin = 99999.0f, melMax = -99999.0f;
        for (float v : melData)
        {
            melMin = std::min(melMin, v);
            melMax = std::max(melMax, v);
        }
        log("Mel stats: min=" + std::to_string(melMin) + " max=" + std::to_string(melMax));
        
        // Prepare f0 input: [batch=1, frames]
        std::vector<int64_t> f0Shape = {1, static_cast<int64_t>(numFrames)};
        std::vector<float> f0Data(f0.begin(), f0.begin() + numFrames);
        
        // Log F0 statistics
        float f0Min = 99999.0f, f0Max = 0.0f, f0Sum = 0.0f;
        int voicedCount = 0;
        for (float freq : f0Data)
        {
            if (freq > 0.0f)
            {
                f0Min = std::min(f0Min, freq);
                f0Max = std::max(f0Max, freq);
                f0Sum += freq;
                voicedCount++;
            }
        }
        log("F0 stats: min=" + std::to_string(f0Min) + 
            " max=" + std::to_string(f0Max) + 
            " mean=" + std::to_string(voicedCount > 0 ? f0Sum / voicedCount : 0.0f) +
            " voiced=" + std::to_string(voicedCount) + "/" + std::to_string(numFrames));
        
        auto endPrep = std::chrono::high_resolution_clock::now();
        auto prepMs = std::chrono::duration_cast<std::chrono::milliseconds>(endPrep - startPrep).count();
        log("Data preparation took " + std::to_string(prepMs) + " ms");
        
        // Create memory info
        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        
        // Create input tensors
        std::vector<Ort::Value> inputTensors;
        inputTensors.push_back(Ort::Value::CreateTensor<float>(
            memoryInfo, melData.data(), melData.size(),
            melShape.data(), melShape.size()));
        inputTensors.push_back(Ort::Value::CreateTensor<float>(
            memoryInfo, f0Data.data(), f0Data.size(),
            f0Shape.data(), f0Shape.size()));
        
        // Run inference
        auto startInfer = std::chrono::high_resolution_clock::now();
        
        auto outputTensors = onnxSession->Run(
            Ort::RunOptions{nullptr},
            inputNames.data(), inputTensors.data(), inputTensors.size(),
            outputNames.data(), outputNames.size());
        
        auto endInfer = std::chrono::high_resolution_clock::now();
        auto inferMs = std::chrono::duration_cast<std::chrono::milliseconds>(endInfer - startInfer).count();
        log("ONNX inference took " + std::to_string(inferMs) + " ms for " + 
            std::to_string(numFrames) + " frames");
        
        // Get output
        if (outputTensors.empty())
        {
            log("ONNX inference returned no output");
            return generateSineFallback(f0);
        }
        
        // Get output tensor info
        auto& outputTensor = outputTensors[0];
        auto typeInfo = outputTensor.GetTensorTypeAndShapeInfo();
        auto outputShape = typeInfo.GetShape();
        size_t outputSize = typeInfo.GetElementCount();
        
        log("ONNX output shape: [" + 
            std::to_string(outputShape.size() > 0 ? outputShape[0] : 0) + ", " +
            std::to_string(outputShape.size() > 1 ? outputShape[1] : 0) + ", " +
            std::to_string(outputShape.size() > 2 ? outputShape[2] : 0) + "]");
        log("Output samples: " + std::to_string(outputSize));
        
        // Copy output to vector
        float* outputData = outputTensor.GetTensorMutableData<float>();
        std::vector<float> waveform(outputData, outputData + outputSize);
        
        // Analyze output statistics before normalization
        float minVal = 0.0f, maxVal = 0.0f, sumAbs = 0.0f;
        for (float sample : waveform)
        {
            minVal = std::min(minVal, sample);
            maxVal = std::max(maxVal, sample);
            sumAbs += std::abs(sample);
        }
        float maxAbs = std::max(std::abs(minVal), std::abs(maxVal));
        float avgAbs = sumAbs / waveform.size();
        
        log("Pre-normalization stats: min=" + std::to_string(minVal) + 
            " max=" + std::to_string(maxVal) +
            " maxAbs=" + std::to_string(maxAbs) +
            " avgAbs=" + std::to_string(avgAbs));
        
        // Normalize output to have consistent volume
        // Target peak around 0.8 to leave headroom
        const float targetPeak = 0.8f;
        
        if (maxAbs > 0.001f)  // Avoid division by zero
        {
            float scale = targetPeak / maxAbs;
            // Don't amplify too much (max 10x gain)
            scale = std::min(scale, 10.0f);
            
            for (float& sample : waveform)
            {
                sample *= scale;
            }
            log("Applied gain scaling: " + std::to_string(scale) + 
                "x (new peak: " + std::to_string(maxAbs * scale) + ")");
        }
        
        // Final safety clamp
        for (float& sample : waveform)
        {
            sample = std::clamp(sample, -1.0f, 1.0f);
        }
        
        auto endTotal = std::chrono::high_resolution_clock::now();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTotal - startTotal).count();
        log("Total vocoder inference took " + std::to_string(totalMs) + " ms");
        
        return waveform;
        
    } catch (const Ort::Exception& e) {
        log("ONNX inference failed: " + std::string(e.what()));
        return generateSineFallback(f0);
    }
#else
    return generateSineFallback(f0);
#endif
}

std::vector<float> Vocoder::inferWithPitchShift(const std::vector<std::vector<float>>& mel,
                                                 const std::vector<float>& f0,
                                                 float pitchShiftSemitones)
{
    if (pitchShiftSemitones == 0.0f)
        return infer(mel, f0);
    
    // Shift F0
    float ratio = std::pow(2.0f, pitchShiftSemitones / 12.0f);
    std::vector<float> shiftedF0 = f0;
    
    for (auto& freq : shiftedF0)
    {
        if (freq > 0.0f)
            freq *= ratio;
    }
    
    return infer(mel, shiftedF0);
}

void Vocoder::inferAsync(const std::vector<std::vector<float>>& mel,
                         const std::vector<float>& f0,
                         std::function<void(std::vector<float>)> callback)
{
    // Run inference in background thread
    std::thread([this, mel, f0, callback]() {
        auto result = this->infer(mel, f0);
        
        // Call callback on message thread
        juce::MessageManager::callAsync([callback, result]() {
            callback(result);
        });
    }).detach();
}

std::vector<float> Vocoder::generateSineFallback(const std::vector<float>& f0)
{
    // Fallback: Generate simple sine wave based on F0
    size_t numFrames = f0.size();
    size_t numSamples = numFrames * hopSize;
    
    std::vector<float> waveform(numSamples, 0.0f);
    
    float phase = 0.0f;
    for (size_t frame = 0; frame < numFrames; ++frame)
    {
        float freq = f0[frame];
        if (freq <= 0.0f) freq = 0.0f;  // Unvoiced
        
        for (int s = 0; s < hopSize; ++s)
        {
            size_t sampleIdx = frame * hopSize + s;
            if (sampleIdx >= numSamples) break;
            
            if (freq > 0.0f)
            {
                waveform[sampleIdx] = 0.3f * std::sin(phase);
                phase += 2.0f * juce::MathConstants<float>::pi * freq / sampleRate;
                if (phase > 2.0f * juce::MathConstants<float>::pi)
                    phase -= 2.0f * juce::MathConstants<float>::pi;
            }
        }
    }
    
    return waveform;
}

void Vocoder::setExecutionDevice(const juce::String& device)
{
    if (executionDevice != device)
    {
        executionDevice = device;
        log("Execution device set to: " + device.toStdString());
    }
}

void Vocoder::setNumThreads(int threads)
{
    if (inferenceThreads != threads)
    {
        inferenceThreads = threads;
        log("Thread count set to: " + std::to_string(threads) + 
            (threads == 0 ? " (auto)" : ""));
    }
}

bool Vocoder::reloadModel()
{
    if (!modelFile.existsAsFile())
    {
        log("Cannot reload: no model file set");
        return false;
    }
    
    log("Reloading model with new settings...");
    
#ifdef HAVE_ONNXRUNTIME
    // Release existing session
    onnxSession.reset();
    inputNames.clear();
    outputNames.clear();
    inputNameStrings.clear();
    outputNameStrings.clear();
    loaded = false;
#endif
    
    return loadModel(modelFile);
}

#ifdef HAVE_ONNXRUNTIME
Ort::SessionOptions Vocoder::createSessionOptions()
{
    Ort::SessionOptions sessionOptions;

    // Set thread count (0 = let onnxruntime decide)
    if (inferenceThreads > 0)
    {
        sessionOptions.SetIntraOpNumThreads(inferenceThreads);
        sessionOptions.SetInterOpNumThreads(inferenceThreads);
    }
    // When inferenceThreads == 0, don't set - let onnxruntime use its default

    // Enable all optimizations
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // Enable memory pattern optimization
    sessionOptions.EnableMemPattern();

    // Enable CPU memory arena
    sessionOptions.EnableCpuMemArena();

    log("Creating session with device: " + executionDevice.toStdString() +
        ", threads: " + (inferenceThreads > 0 ? std::to_string(inferenceThreads) : "auto"));
    
    // Add execution provider based on device selection
    if (executionDevice == "CUDA")
    {
        try {
            OrtCUDAProviderOptions cudaOptions{};
            cudaOptions.device_id = 0;
            sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
            log("CUDA execution provider added");
        } catch (const Ort::Exception& e) {
            log("Failed to add CUDA provider: " + std::string(e.what()));
            log("Falling back to CPU");
        }
    }
    else if (executionDevice == "DirectML")
    {
        try {
            sessionOptions.AppendExecutionProvider("DML");
            log("DirectML execution provider added");
        } catch (const Ort::Exception& e) {
            log("Failed to add DirectML provider: " + std::string(e.what()));
            log("Falling back to CPU");
        }
    }
    else if (executionDevice == "CoreML")
    {
        try {
            sessionOptions.AppendExecutionProvider("CoreML");
            log("CoreML execution provider added");
        } catch (const Ort::Exception& e) {
            log("Failed to add CoreML provider: " + std::string(e.what()));
            log("Falling back to CPU");
        }
    }
    else if (executionDevice == "TensorRT")
    {
        try {
            OrtTensorRTProviderOptions trtOptions{};
            sessionOptions.AppendExecutionProvider_TensorRT(trtOptions);
            log("TensorRT execution provider added");
        } catch (const Ort::Exception& e) {
            log("Failed to add TensorRT provider: " + std::string(e.what()));
            log("Falling back to CPU");
        }
    }
    // CPU is the default fallback
    
    return sessionOptions;
}
#endif
