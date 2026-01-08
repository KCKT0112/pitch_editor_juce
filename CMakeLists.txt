cmake_minimum_required(VERSION 3.22)
project(PitchEditor VERSION 1.0.0)

# Add JUCE as a subdirectory (you need to clone JUCE here)
# git clone https://github.com/juce-framework/JUCE.git
add_subdirectory(JUCE)

# ONNX Runtime configuration
set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to ONNX Runtime")

# If not set, try to find via environment variable or default location
if(NOT ONNXRUNTIME_ROOT)
    if(DEFINED ENV{ONNXRUNTIME_ROOT})
        set(ONNXRUNTIME_ROOT $ENV{ONNXRUNTIME_ROOT})
    else()
        # Default paths for different platforms
        if(WIN32)
            set(ONNXRUNTIME_ROOT "C:/onnxruntime" CACHE PATH "Path to ONNX Runtime" FORCE)
        else()
            set(ONNXRUNTIME_ROOT "/usr/local" CACHE PATH "Path to ONNX Runtime" FORCE)
        endif()
    endif()
endif()

# Find ONNX Runtime
find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS ${ONNXRUNTIME_ROOT}/include
    NO_DEFAULT_PATH)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    PATHS ${ONNXRUNTIME_ROOT}/lib
    NO_DEFAULT_PATH)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    set(ONNXRUNTIME_FOUND TRUE)
    message(STATUS "Found ONNX Runtime:")
    message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "  Library: ${ONNXRUNTIME_LIBRARY}")
else()
    set(ONNXRUNTIME_FOUND FALSE)
    message(WARNING "ONNX Runtime not found. Set ONNXRUNTIME_ROOT to the installation directory.")
    message(WARNING "Vocoder inference will be disabled.")
endif()

# Create GUI application
juce_add_gui_app(PitchEditor
    PRODUCT_NAME "Pitch Editor"
    COMPANY_NAME "OpenVPI"
    BUNDLE_ID "com.openvpi.pitcheditor"
    COMPANY_COPYRIGHT "Copyright 2026"
    MICROPHONE_PERMISSION_ENABLED TRUE
    FILE_SHARING_ENABLED TRUE
    DOCUMENT_EXTENSIONS "wav;mp3;flac;ogg")

# Add source files
target_sources(PitchEditor PRIVATE
    Source/Main.cpp
    Source/JuceHeader.h
    Source/Audio/AudioEngine.cpp
    Source/Audio/AudioEngine.h
    Source/Audio/Vocoder.cpp
    Source/Audio/Vocoder.h
    Source/Audio/PitchDetector.cpp
    Source/Audio/PitchDetector.h
    Source/Audio/FCPEPitchDetector.cpp
    Source/Audio/FCPEPitchDetector.h
    Source/UI/MainComponent.cpp
    Source/UI/MainComponent.h
    Source/UI/PianoRollComponent.cpp
    Source/UI/PianoRollComponent.h
    Source/UI/WaveformComponent.cpp
    Source/UI/WaveformComponent.h
    Source/UI/ToolbarComponent.cpp
    Source/UI/ToolbarComponent.h
    Source/UI/ParameterPanel.cpp
    Source/UI/ParameterPanel.h
    Source/UI/SettingsComponent.cpp
    Source/UI/SettingsComponent.h
    Source/Models/Project.cpp
    Source/Models/Project.h
    Source/Models/Note.cpp
    Source/Models/Note.h
    Source/Utils/Constants.h
    Source/Utils/MelSpectrogram.cpp
    Source/Utils/MelSpectrogram.h)

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

# Link ONNX Runtime if found
if(ONNXRUNTIME_FOUND)
    target_include_directories(PitchEditor PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(PitchEditor PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(PitchEditor PRIVATE HAVE_ONNXRUNTIME=1)
    
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
            endforeach()
        else()
            message(WARNING "No ONNX Runtime DLLs found to copy")
        endif()
    endif()
endif()

# Set C++ standard
target_compile_features(PitchEditor PRIVATE cxx_std_17)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(PitchEditor PRIVATE
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_ASIO=0)
elseif(APPLE)
    target_compile_definitions(PitchEditor PRIVATE
        JUCE_COREAUDIO=1)
else()
    target_compile_definitions(PitchEditor PRIVATE
        JUCE_ALSA=1
        JUCE_JACK=1)
endif()

# Common definitions
target_compile_definitions(PitchEditor PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PitchEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PitchEditor,JUCE_VERSION>")

# ONNX model path
set(ONNX_MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/models/pc_nsf_hifigan.onnx")
if(EXISTS ${ONNX_MODEL_PATH})
    message(STATUS "Found ONNX model: ${ONNX_MODEL_PATH}")
    # Copy model to output directory
    add_custom_command(TARGET PitchEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PitchEditor>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNX_MODEL_PATH}"
        "$<TARGET_FILE_DIR:PitchEditor>/models/pc_nsf_hifigan.onnx")
else()
    message(WARNING "ONNX model not found at ${ONNX_MODEL_PATH}")
endif()

# FCPE model and resources
set(FCPE_MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/models/fcpe.onnx")
set(FCPE_MEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/models/mel_filterbank.bin")
set(FCPE_CENT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/models/cent_table.bin")

if(EXISTS ${FCPE_MODEL_PATH})
    message(STATUS "Found FCPE model: ${FCPE_MODEL_PATH}")
    add_custom_command(TARGET PitchEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PitchEditor>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FCPE_MODEL_PATH}"
        "$<TARGET_FILE_DIR:PitchEditor>/models/fcpe.onnx")
else()
    message(WARNING "FCPE model not found at ${FCPE_MODEL_PATH}")
endif()

if(EXISTS ${FCPE_MEL_PATH})
    add_custom_command(TARGET PitchEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FCPE_MEL_PATH}"
        "$<TARGET_FILE_DIR:PitchEditor>/models/mel_filterbank.bin")
endif()

if(EXISTS ${FCPE_CENT_PATH})
    add_custom_command(TARGET PitchEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FCPE_CENT_PATH}"
        "$<TARGET_FILE_DIR:PitchEditor>/models/cent_table.bin")
endif()
