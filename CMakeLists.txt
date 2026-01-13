cmake_minimum_required(VERSION 3.22)
project(PitchEditor VERSION 1.0.0)

include(FetchContent)

# ONNX Runtime configuration
option(USE_GPU "Enable GPU acceleration for ONNX Runtime (not supported on macOS)" OFF)
option(USE_CUDA "Enable CUDA execution provider" OFF)
option(USE_DIRECTML "Enable DirectML execution provider (Windows only)" OFF)
option(USE_BUNDLED_CUDA_RUNTIME "Bundle minimal CUDA runtime DLLs (Windows only)" OFF)
option(USE_BUNDLED_DIRECTML_RUNTIME "Bundle DirectML runtime DLL (Windows only)" OFF)
set(CUDA_REDIST_URL "" CACHE STRING "Optional URL to download CUDA runtime redistributable zip")
set(DIRECTML_REDIST_URL "" CACHE STRING "Optional URL to download DirectML redistributable zip")
set(ONNXRUNTIME_VERSION "1.17.3" CACHE STRING "ONNX Runtime version")

# Check that CUDA and DirectML are not both enabled
if(USE_CUDA AND USE_DIRECTML)
    message(FATAL_ERROR "ERROR: USE_CUDA and USE_DIRECTML cannot be enabled at the same time. "
                        "DirectML requires a separate version of ONNX Runtime. "
                        "Please enable only one of them.")
endif()

# If CUDA is requested, force GPU pipeline on
if(USE_CUDA)
    set(USE_GPU ON)
    message(STATUS "USE_CUDA=ON -> enabling USE_GPU automatically")
endif()

# If DirectML is requested, force GPU pipeline on
if(USE_DIRECTML)
    set(USE_GPU ON)
    message(STATUS "USE_DIRECTML=ON -> enabling USE_GPU automatically")
endif()

# Check if ONNX Runtime already exists in build directory
set(ONNXRUNTIME_CACHE_DIR "${CMAKE_BINARY_DIR}/_deps/onnxruntime-src")
if(EXISTS "${ONNXRUNTIME_CACHE_DIR}/include/onnxruntime_cxx_api.h")
    message(STATUS "ONNX Runtime already downloaded, skipping download")
    set(ONNXRUNTIME_ROOT ${ONNXRUNTIME_CACHE_DIR})
else()
    # Detect platform and set download URL
    if(WIN32)
        if(USE_CUDA)
            # CUDA requires CUDA-specific ONNX Runtime build
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-gpu-cuda12-${ONNXRUNTIME_VERSION}.zip")
            message(STATUS "Downloading ONNX Runtime GPU CUDA12 package (Windows)")
        elseif(USE_DIRECTML)
            # DirectML uses the NuGet package (NuGet packages are ZIP files)
            set(ONNXRUNTIME_URL "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${ONNXRUNTIME_VERSION}")
            message(STATUS "Downloading ONNX Runtime DirectML package from NuGet (Windows)")
        else()
            # CPU-only package
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
            message(STATUS "Downloading ONNX Runtime CPU package (Windows)")
        endif()
    elseif(APPLE)
        if(USE_GPU)
            message(WARNING "GPU acceleration is not supported on macOS, falling back to CPU")
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
        else()
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-x86_64-${ONNXRUNTIME_VERSION}.tgz")
        endif()
    else()
        if(USE_GPU)
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-gpu-${ONNXRUNTIME_VERSION}.tgz")
        else()
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
        endif()
    endif()

    message(STATUS "Downloading ONNX Runtime from: ${ONNXRUNTIME_URL}")

    FetchContent_Declare(
        onnxruntime
        URL ${ONNXRUNTIME_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(onnxruntime)
    set(ONNXRUNTIME_ROOT ${onnxruntime_SOURCE_DIR})
endif()

# Handle different directory structures for DirectML NuGet package vs regular packages
if(WIN32 AND USE_DIRECTML)
    # DirectML NuGet package has different structure: runtimes/win-x64/native/
    set(ONNXRUNTIME_INCLUDE_DIR ${ONNXRUNTIME_ROOT}/build/native/include)
    set(ONNXRUNTIME_LIBRARY ${ONNXRUNTIME_ROOT}/runtimes/win-x64/native/onnxruntime.lib)
    set(ONNXRUNTIME_DLL_DIR ${ONNXRUNTIME_ROOT}/runtimes/win-x64/native)
else()
    set(ONNXRUNTIME_INCLUDE_DIR ${ONNXRUNTIME_ROOT}/include)
    if(WIN32)
        set(ONNXRUNTIME_LIBRARY ${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib)
        set(ONNXRUNTIME_DLL_DIR ${ONNXRUNTIME_ROOT}/lib)
    else()
        find_library(ONNXRUNTIME_LIBRARY onnxruntime PATHS ${ONNXRUNTIME_ROOT}/lib NO_DEFAULT_PATH)
    endif()
endif()

if(EXISTS ${ONNXRUNTIME_INCLUDE_DIR} AND ONNXRUNTIME_LIBRARY)
    set(ONNXRUNTIME_FOUND TRUE)
    message(STATUS "ONNX Runtime configured:")
    message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "  Library: ${ONNXRUNTIME_LIBRARY}")
    if(USE_GPU AND NOT APPLE)
        message(STATUS "  GPU acceleration: ENABLED")
    else()
        message(STATUS "  GPU acceleration: DISABLED")
    endif()
else()
    set(ONNXRUNTIME_FOUND FALSE)
    message(WARNING "ONNX Runtime configuration failed")
endif()

# Add JUCE as a subdirectory (git submodule in third_party/)
add_subdirectory(third_party/JUCE)

# Create GUI application
juce_add_gui_app(PitchEditor
    PRODUCT_NAME "HachiTune"
    COMPANY_NAME "OpenVPI"
    BUNDLE_ID "com.openvpi.hachitune"
    COMPANY_COPYRIGHT "Copyright 2026"
    MICROPHONE_PERMISSION_ENABLED TRUE
    FILE_SHARING_ENABLED TRUE
    DOCUMENT_EXTENSIONS "wav;mp3;flac;ogg")

# AAX SDK configuration (user must set AAX_SDK_PATH environment variable)
if(DEFINED ENV{AAX_SDK_PATH} AND EXISTS "$ENV{AAX_SDK_PATH}")
    juce_set_aax_sdk_path($ENV{AAX_SDK_PATH})
    set(AAX_AVAILABLE TRUE)
    message(STATUS "AAX SDK found at: $ENV{AAX_SDK_PATH}")
else()
    set(AAX_AVAILABLE FALSE)
    message(STATUS "AAX SDK not found - AAX format will not be built")
endif()

# ARA SDK configuration (bundled as submodule)
set(ARA_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ARA_SDK")
if(EXISTS "${ARA_SDK_PATH}/ARA_API")
    juce_set_ara_sdk_path(${ARA_SDK_PATH})
    set(ARA_AVAILABLE TRUE)
    message(STATUS "ARA SDK found at: ${ARA_SDK_PATH}")
else()
    set(ARA_AVAILABLE FALSE)
    message(STATUS "ARA SDK not found - run: git submodule update --init --recursive")
endif()

# Create VST3 plugin
juce_add_plugin(PitchEditorPlugin
    PRODUCT_NAME "HachiTune"
    COMPANY_NAME "OpenVPI"
    BUNDLE_ID "com.openvpi.hachitune"
    PLUGIN_MANUFACTURER_CODE OPVP
    PLUGIN_CODE PTE1
    FORMATS VST3 AU $<$<BOOL:${AAX_AVAILABLE}>:AAX>
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_ARA_EFFECT $<BOOL:${ARA_AVAILABLE}>
    MICROPHONE_PERMISSION_ENABLED TRUE
    FILE_SHARING_ENABLED TRUE)

# ARA compile definitions
if(ARA_AVAILABLE)
    target_compile_definitions(PitchEditorPlugin PRIVATE
        JucePlugin_Enable_ARA=1)
endif()

# Add source files - platform-specific files
set(MAIN_SOURCE Source/Main.cpp)
set(MAINCOMPONENT_SOURCE Source/UI/MainComponent.cpp)
set(TOOLBAR_SOURCE Source/UI/ToolbarComponent.cpp)

set(PITCH_EDITOR_COMMON_SOURCES
    Source/JuceHeader.h
    Source/Plugin/HostCompatibility.h
    Source/Plugin/HostCompatibility.cpp
    # Audio - Engine
    Source/Audio/AudioEngine.cpp
    Source/Audio/AudioEngine.h
    Source/Audio/Engine/PlaybackController.cpp
    Source/Audio/Engine/PlaybackController.h
    # Audio - Analysis
    Source/Audio/PitchDetector.cpp
    Source/Audio/PitchDetector.h
    Source/Audio/PitchDetectorType.h
    Source/Audio/FCPEPitchDetector.cpp
    Source/Audio/FCPEPitchDetector.h
    Source/Audio/RMVPEPitchDetector.cpp
    Source/Audio/RMVPEPitchDetector.h
    Source/Audio/SOMEDetector.cpp
    Source/Audio/SOMEDetector.h
    Source/Audio/RealtimePitchProcessor.cpp
    Source/Audio/RealtimePitchProcessor.h
    Source/Audio/Analysis/AudioAnalyzer.cpp
    Source/Audio/Analysis/AudioAnalyzer.h
    # Audio - Synthesis
    Source/Audio/Vocoder.cpp
    Source/Audio/Vocoder.h
    Source/Audio/Synthesis/IncrementalSynthesizer.cpp
    Source/Audio/Synthesis/IncrementalSynthesizer.h
    # Audio - IO
    Source/Audio/IO/AudioFileManager.cpp
    Source/Audio/IO/AudioFileManager.h
    # UI - Main
    Source/UI/CustomTitleBar.cpp
    Source/UI/CustomTitleBar.h
    ${MAINCOMPONENT_SOURCE}
    Source/UI/MainComponent.h
    Source/UI/Main/MenuHandler.cpp
    Source/UI/Main/MenuHandler.h
    Source/UI/Main/SettingsManager.cpp
    Source/UI/Main/SettingsManager.h
    # UI - PianoRoll
    Source/UI/PianoRollComponent.cpp
    Source/UI/PianoRollComponent.h
    Source/UI/PianoRoll/CoordinateMapper.h
    Source/UI/PianoRoll/PianoRollRenderer.cpp
    Source/UI/PianoRoll/PianoRollRenderer.h
    Source/UI/PianoRoll/ScrollZoomController.cpp
    Source/UI/PianoRoll/ScrollZoomController.h
    Source/UI/PianoRoll/PitchEditor.cpp
    Source/UI/PianoRoll/PitchEditor.h
    # UI - Components
    Source/UI/WaveformComponent.cpp
    Source/UI/WaveformComponent.h
    ${TOOLBAR_SOURCE}
    Source/UI/ToolbarComponent.h
    Source/UI/ParameterPanel.cpp
    Source/UI/ParameterPanel.h
    Source/UI/SettingsComponent.cpp
    Source/UI/SettingsComponent.h
    Source/UI/StyledComponents.cpp
    Source/UI/StyledComponents.h
    # Models
    Source/Models/Project.cpp
    Source/Models/Project.h
    Source/Models/Note.cpp
    Source/Models/Note.h
    # Utils
    Source/Utils/DrawCurve.h
    Source/Utils/Constants.h
    Source/Utils/AppLogger.h
    Source/Utils/PlatformPaths.h
    Source/Utils/UndoManager.h
    Source/Utils/Localization.h
    Source/Utils/MelSpectrogram.cpp
    Source/Utils/MelSpectrogram.h
    Source/Utils/BasePitchCurve.cpp
    Source/Utils/BasePitchCurve.h
    Source/Utils/F0Smoother.cpp
    Source/Utils/F0Smoother.h
    Source/Utils/PitchCurveProcessor.cpp
    Source/Utils/PitchCurveProcessor.h
    Source/Utils/SinusoidalSmoothing.cpp
    Source/Utils/SinusoidalSmoothing.h)

target_sources(PitchEditor PRIVATE
    ${MAIN_SOURCE}
    ${PITCH_EDITOR_COMMON_SOURCES})

target_sources(PitchEditorPlugin PRIVATE
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor.h
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/PluginEditor.h
    Source/Plugin/ARADocumentController.cpp
    Source/Plugin/ARADocumentController.h
    ${PITCH_EDITOR_COMMON_SOURCES})

# Link JUCE modules
target_link_libraries(PitchEditor PRIVATE
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_link_libraries(PitchEditorPlugin PRIVATE
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_plugin_client
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Link ONNX Runtime if found
if(ONNXRUNTIME_FOUND)
    target_include_directories(PitchEditor PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(PitchEditor PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(PitchEditor PRIVATE HAVE_ONNXRUNTIME=1)

    target_include_directories(PitchEditorPlugin PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(PitchEditorPlugin PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(PitchEditorPlugin PRIVATE HAVE_ONNXRUNTIME=1)

    # Pass GPU option to code (macOS always uses CPU)
    if(USE_GPU AND NOT APPLE)
        target_compile_definitions(PitchEditor PRIVATE ONNXRUNTIME_USE_GPU=1)
        target_compile_definitions(PitchEditorPlugin PRIVATE ONNXRUNTIME_USE_GPU=1)
    endif()

    # Enable CUDA if requested
    if(USE_CUDA)
        target_compile_definitions(PitchEditor PRIVATE USE_CUDA=1)
        target_compile_definitions(PitchEditorPlugin PRIVATE USE_CUDA=1)
        message(STATUS "CUDA execution provider: ENABLED")
    endif()

    # Enable DirectML if requested (Windows only)
    if(USE_DIRECTML AND WIN32)
        target_compile_definitions(PitchEditor PRIVATE USE_DIRECTML=1)
        target_compile_definitions(PitchEditorPlugin PRIVATE USE_DIRECTML=1)
        message(STATUS "DirectML execution provider: ENABLED")
    endif()

    # Copy ONNX Runtime DLLs to output directory on Windows
    if(WIN32)
        get_filename_component(ONNXRUNTIME_LIB_DIR ${ONNXRUNTIME_LIBRARY} DIRECTORY)
        # Try lib directory first (most common), then bin directory
        file(GLOB ONNXRUNTIME_DLLS "${ONNXRUNTIME_LIB_DIR}/*.dll")
        if(NOT ONNXRUNTIME_DLLS)
            file(GLOB ONNXRUNTIME_DLLS "${ONNXRUNTIME_LIB_DIR}/../bin/*.dll")
        endif()
        if(ONNXRUNTIME_DLLS)
            foreach(DLL ${ONNXRUNTIME_DLLS})
                message(STATUS "Will copy ONNX Runtime DLL: ${DLL}")
                add_custom_command(TARGET PitchEditor POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}"
                    "$<TARGET_FILE_DIR:PitchEditor>")

                add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}"
                    "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>")
            endforeach()
        else()
            message(WARNING "No ONNX Runtime DLLs found to copy")
        endif()
        
        # Copy CUDA runtime DLLs if CUDA is enabled
        if(USE_CUDA)
            # Optional bundled CUDA runtime (redistributable) to avoid system dependency
            # If USE_BUNDLED_CUDA_RUNTIME is ON, we first look for a local redistributable dir,
            # otherwise try to download from CUDA_REDIST_URL (if provided).
            if(USE_BUNDLED_CUDA_RUNTIME)
                set(CUDA_REDIST_DIR "${CMAKE_SOURCE_DIR}/vendor/cuda/bin")
                if(NOT EXISTS "${CUDA_REDIST_DIR}")
                    set(CUDA_REDIST_DIR "${CMAKE_BINARY_DIR}/cuda_redist/bin")
                endif()

                # Download redistributable if URL is provided and dir not present
                if(NOT EXISTS "${CUDA_REDIST_DIR}" AND CUDA_REDIST_URL)
                    message(STATUS "Downloading CUDA runtime redistributable from ${CUDA_REDIST_URL}")
                    file(DOWNLOAD "${CUDA_REDIST_URL}"
                        "${CMAKE_BINARY_DIR}/cuda_redist.zip"
                        SHOW_PROGRESS)
                    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cuda_redist")
                    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_BINARY_DIR}/cuda_redist.zip"
                                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/cuda_redist")
                    # Try common layout: cuda_redist/bin
                    if(EXISTS "${CMAKE_BINARY_DIR}/cuda_redist/bin")
                        set(CUDA_REDIST_DIR "${CMAKE_BINARY_DIR}/cuda_redist/bin")
                    endif()
                endif()
            endif()

            # Common CUDA installation paths
            set(CUDA_PATHS
                "$ENV{CUDA_PATH}/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.0/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.1/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.2/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.9/bin"
            )
            
            # CUDA runtime DLLs needed by ONNX Runtime
            set(CUDA_DLLS
                "cublasLt64_12.dll"
                "cublas64_12.dll"
                "cudnn64_8.dll"
                "cufft64_12.dll"
                "curand64_12.dll"
                "cusolver64_12.dll"
                "cusparse64_12.dll"
                "cudart64_12.dll"
            )
            
            # Also try version 11 DLLs
            list(APPEND CUDA_DLLS
                "cublasLt64_11.dll"
                "cublas64_11.dll"
                "cufft64_11.dll"
                "curand64_11.dll"
                "cusolver64_11.dll"
                "cusparse64_11.dll"
                "cudart64_11.dll"
            )
            
            # Find CUDA bin directory (prefer bundled redistributable if present)
            set(CUDA_BIN_DIR "")
            if(USE_BUNDLED_CUDA_RUNTIME AND EXISTS "${CUDA_REDIST_DIR}")
                set(CUDA_BIN_DIR "${CUDA_REDIST_DIR}")
                message(STATUS "Using bundled CUDA runtime from: ${CUDA_BIN_DIR}")
            endif()

            foreach(CUDA_PATH ${CUDA_PATHS})
                if(NOT CUDA_BIN_DIR AND EXISTS "${CUDA_PATH}")
                    set(CUDA_BIN_DIR "${CUDA_PATH}")
                    message(STATUS "Found CUDA bin directory: ${CUDA_BIN_DIR}")
                    break()
                endif()
            endforeach()
            
            if(CUDA_BIN_DIR)
                foreach(CUDA_DLL ${CUDA_DLLS})
                    set(CUDA_DLL_PATH "${CUDA_BIN_DIR}/${CUDA_DLL}")
                    if(EXISTS "${CUDA_DLL_PATH}")
                        message(STATUS "Will copy CUDA DLL: ${CUDA_DLL}")
                        add_custom_command(TARGET PitchEditor POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${CUDA_DLL_PATH}"
                            "$<TARGET_FILE_DIR:PitchEditor>")
                        
                        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${CUDA_DLL_PATH}"
                            "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>")
                    endif()
                endforeach()
            else()
                message(WARNING "CUDA bin directory not found. CUDA runtime DLLs will not be copied.")
                message(WARNING "Please ensure CUDA Toolkit is installed and CUDA_PATH environment variable is set.")
                message(WARNING "Or manually copy CUDA DLLs from CUDA installation to the output directory.")
            endif()
        endif()

        # Copy DirectML runtime DLL if explicitly requested (Windows only)
        # Note: DirectML is typically included in Windows 10/11 and will be loaded from the system
        if(USE_DIRECTML AND USE_BUNDLED_DIRECTML_RUNTIME)
            set(DML_DLLS "DirectML.dll")
            set(DML_BIN_DIR "")

            set(DML_REDIST_DIR "${CMAKE_SOURCE_DIR}/vendor/directml/bin")
            if(NOT EXISTS "${DML_REDIST_DIR}")
                set(DML_REDIST_DIR "${CMAKE_BINARY_DIR}/directml_redist/bin")
            endif()

            # Download redistributable if URL provided and dir not present
            if(NOT EXISTS "${DML_REDIST_DIR}" AND DIRECTML_REDIST_URL)
                message(STATUS "Downloading DirectML redistributable from ${DIRECTML_REDIST_URL}")
                file(DOWNLOAD "${DIRECTML_REDIST_URL}"
                    "${CMAKE_BINARY_DIR}/directml_redist.zip"
                    SHOW_PROGRESS)
                file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/directml_redist")
                execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_BINARY_DIR}/directml_redist.zip"
                                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/directml_redist")
                if(EXISTS "${CMAKE_BINARY_DIR}/directml_redist/bin")
                    set(DML_REDIST_DIR "${CMAKE_BINARY_DIR}/directml_redist/bin")
                endif()
            endif()

            if(EXISTS "${DML_REDIST_DIR}")
                set(DML_BIN_DIR "${DML_REDIST_DIR}")
                message(STATUS "Using bundled DirectML runtime from: ${DML_BIN_DIR}")

                foreach(DML_DLL ${DML_DLLS})
                    set(DML_DLL_PATH "${DML_BIN_DIR}/${DML_DLL}")
                    if(EXISTS "${DML_DLL_PATH}")
                        message(STATUS "Will copy DirectML DLL: ${DML_DLL_PATH}")
                        add_custom_command(TARGET PitchEditor POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${DML_DLL_PATH}"
                            "$<TARGET_FILE_DIR:PitchEditor>")

                        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${DML_DLL_PATH}"
                            "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>")
                    endif()
                endforeach()
            else()
                message(WARNING "USE_BUNDLED_DIRECTML_RUNTIME is ON but DirectML redistributable not found.")
                message(WARNING "Set DIRECTML_REDIST_URL or place DirectML.dll in vendor/directml/bin/")
            endif()
        elseif(USE_DIRECTML)
            message(STATUS "DirectML will be loaded from the system (included in Windows 10/11)")
        endif()
    elseif(APPLE)
        # macOS: Embed dylib in app bundle and plugin bundles
        get_filename_component(ONNXRUNTIME_LIB_DIR ${ONNXRUNTIME_LIBRARY} DIRECTORY)
        file(GLOB ONNXRUNTIME_DYLIBS "${ONNXRUNTIME_LIB_DIR}/*.dylib")
        if(ONNXRUNTIME_DYLIBS)
            foreach(DYLIB ${ONNXRUNTIME_DYLIBS})
                message(STATUS "Will embed ONNX Runtime dylib: ${DYLIB}")
                # Standalone app
                add_custom_command(TARGET PitchEditor POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Frameworks"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DYLIB}"
                    "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Frameworks/")
                # VST3 plugin
                add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Frameworks"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DYLIB}"
                    "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Frameworks/")
                # AU plugin
                add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Frameworks"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DYLIB}"
                    "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Frameworks/")
            endforeach()
            # Fix rpath for all targets using install_name_tool
            add_custom_command(TARGET PitchEditor POST_BUILD
                COMMAND install_name_tool -add_rpath "@executable_path/../Frameworks" "$<TARGET_FILE:PitchEditor>" 2>/dev/null || true)
            add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
                COMMAND install_name_tool -add_rpath "@loader_path/../Frameworks" "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/MacOS/HachiTune" 2>/dev/null || true)
            add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
                COMMAND install_name_tool -add_rpath "@loader_path/../Frameworks" "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/MacOS/HachiTune" 2>/dev/null || true)
        endif()
    endif()
endif()

# Set C++ standard
target_compile_features(PitchEditor PRIVATE cxx_std_17)
target_compile_features(PitchEditorPlugin PRIVATE cxx_std_17)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(PitchEditor PRIVATE
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_ASIO=0)

    target_compile_definitions(PitchEditorPlugin PRIVATE
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_ASIO=0)
elseif(APPLE)
    target_compile_definitions(PitchEditor PRIVATE
        JUCE_COREAUDIO=1)

    target_compile_definitions(PitchEditorPlugin PRIVATE
        JUCE_COREAUDIO=1)
else()
    target_compile_definitions(PitchEditor PRIVATE
        JUCE_ALSA=1
        JUCE_JACK=1)

    target_compile_definitions(PitchEditorPlugin PRIVATE
        JUCE_ALSA=1
        JUCE_JACK=1)
endif()

# Common definitions
target_compile_definitions(PitchEditor PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PitchEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PitchEditor,JUCE_VERSION>")

target_compile_definitions(PitchEditorPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PitchEditorPlugin,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PitchEditorPlugin,JUCE_VERSION>")

# Download models if not present
set(MODELS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/models")
set(MODELS_ZIP "${CMAKE_BINARY_DIR}/models.zip")
set(REQUIRED_MODELS
    "${MODELS_DIR}/pc_nsf_hifigan.onnx"
    "${MODELS_DIR}/fcpe.onnx"
    "${MODELS_DIR}/some.onnx"
)

# Check if any required model is missing
set(MODELS_MISSING FALSE)
foreach(MODEL ${REQUIRED_MODELS})
    if(NOT EXISTS ${MODEL})
        set(MODELS_MISSING TRUE)
        message(STATUS "Missing model: ${MODEL}")
    endif()
endforeach()

if(MODELS_MISSING)
    message(FATAL_ERROR "Required models not found in ${MODELS_DIR}. "
                        "Please ensure the models folder exists with the following files:\n"
                        "  - pc_nsf_hifigan.onnx\n"
                        "  - fcpe.onnx\n"
                        "  - some.onnx")
else()
    message(STATUS "All required models found")
endif()

# ONNX model path
set(ONNX_MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Resources/models/pc_nsf_hifigan.onnx")
if(EXISTS ${ONNX_MODEL_PATH})
    message(STATUS "Found ONNX model: ${ONNX_MODEL_PATH}")
    # Copy model to output directory
    if(APPLE)
        # macOS: Copy to Resources folder in app bundle
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Resources/models/pc_nsf_hifigan.onnx")
        # VST3 plugin
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Resources/models/pc_nsf_hifigan.onnx")
        # AU plugin
        add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Resources/models/pc_nsf_hifigan.onnx")
    else()
        # Windows/Linux: Copy next to executable
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PitchEditor>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_MODEL_PATH}"
            "$<TARGET_FILE_DIR:PitchEditor>/models/pc_nsf_hifigan.onnx")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNX_MODEL_PATH}"
            "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>/models/pc_nsf_hifigan.onnx")
    endif()
else()
    message(WARNING "ONNX model not found at ${ONNX_MODEL_PATH}")
endif()

# FCPE model and resources
set(FCPE_MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Resources/models/fcpe.onnx")
set(FCPE_MEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Resources/models/mel_filterbank.bin")
set(FCPE_CENT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Resources/models/cent_table.bin")

if(EXISTS ${FCPE_MODEL_PATH})
    message(STATUS "Found FCPE model: ${FCPE_MODEL_PATH}")
    if(APPLE)
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Resources/models/fcpe.onnx")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Resources/models/fcpe.onnx")
        add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Resources/models/fcpe.onnx")
    else()
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PitchEditor>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MODEL_PATH}"
            "$<TARGET_FILE_DIR:PitchEditor>/models/fcpe.onnx")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MODEL_PATH}"
            "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>/models/fcpe.onnx")
    endif()
else()
    message(WARNING "FCPE model not found at ${FCPE_MODEL_PATH}")
endif()

if(EXISTS ${FCPE_MEL_PATH})
    if(APPLE)
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Resources/models/mel_filterbank.bin")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Resources/models/mel_filterbank.bin")
        add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Resources/models/mel_filterbank.bin")
    else()
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MEL_PATH}"
            "$<TARGET_FILE_DIR:PitchEditor>/models/mel_filterbank.bin")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_MEL_PATH}"
            "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>/models/mel_filterbank.bin")
    endif()
endif()

if(EXISTS ${FCPE_CENT_PATH})
    if(APPLE)
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_CENT_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Resources/models/cent_table.bin")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_CENT_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Resources/models/cent_table.bin")
        add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_CENT_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Resources/models/cent_table.bin")
    else()
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_CENT_PATH}"
            "$<TARGET_FILE_DIR:PitchEditor>/models/cent_table.bin")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FCPE_CENT_PATH}"
            "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>/models/cent_table.bin")
    endif()
endif()

# SOME model
set(SOME_MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Resources/models/some.onnx")
if(EXISTS ${SOME_MODEL_PATH})
    message(STATUS "Found SOME model: ${SOME_MODEL_PATH}")
    if(APPLE)
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SOME_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Resources/models/some.onnx")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SOME_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Resources/models/some.onnx")
        add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SOME_MODEL_PATH}"
            "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Resources/models/some.onnx")
    else()
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SOME_MODEL_PATH}"
            "$<TARGET_FILE_DIR:PitchEditor>/models/some.onnx")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SOME_MODEL_PATH}"
            "$<TARGET_FILE_DIR:PitchEditorPlugin_VST3>/models/some.onnx")
    endif()
else()
    message(WARNING "SOME model not found at ${SOME_MODEL_PATH}")
endif()

# Copy language files
set(LANG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/lang")
file(GLOB LANG_FILES "${LANG_DIR}/*.json")
if(LANG_FILES)
    message(STATUS "Found language files: ${LANG_FILES}")
    if(APPLE)
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Resources/lang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LANG_FILES} "$<TARGET_BUNDLE_DIR:PitchEditor>/Contents/Resources/lang/")
        add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Resources/lang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LANG_FILES} "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>/Contents/Resources/lang/")
        add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Resources/lang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LANG_FILES} "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>/Contents/Resources/lang/")
    else()
        add_custom_command(TARGET PitchEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PitchEditor>/lang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LANG_FILES} "$<TARGET_FILE_DIR:PitchEditor>/lang/")
    endif()
endif()

# Re-sign bundles on macOS after all resources are copied
if(APPLE)
    add_custom_command(TARGET PitchEditor POST_BUILD
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:PitchEditor>" 2>/dev/null || true
        COMMENT "Re-signing PitchEditor bundle")
    add_custom_command(TARGET PitchEditorPlugin_VST3 POST_BUILD
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_VST3>" 2>/dev/null || true
        COMMENT "Re-signing VST3 bundle")
    add_custom_command(TARGET PitchEditorPlugin_AU POST_BUILD
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:PitchEditorPlugin_AU>" 2>/dev/null || true
        COMMENT "Re-signing AU bundle")
endif()
