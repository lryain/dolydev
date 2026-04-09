/**
 * @file Vivoka.h
 * @brief Vivoka VSDK 统一头文件
 * 
 * 此文件提供了Vivoka VSDK所有主要API的统一访问入口。
 */

#pragma once

// ASR API
#include <vivoka/vsdk/asr/Engine.hpp>
#include <vivoka/vsdk/asr/Recognizer.hpp>
#include <vivoka/vsdk/asr/DynamicModel.hpp>

// TTS API
#include <vivoka/vsdk/tts/Engine.hpp>
#include <vivoka/vsdk/tts/Channel.hpp>
#include <vivoka/vsdk/tts/Events.hpp>
#include <vivoka/vsdk/tts/Utils.hpp>

// NLU API
#include <vivoka/vsdk/nlu/Engine.hpp>
#include <vivoka/vsdk/nlu/Parser.hpp>
#include <vivoka/vsdk/nlu/Result.hpp>

// Audio API
#include <vivoka/vsdk/audio/Buffer.hpp>
#include <vivoka/vsdk/audio/Pipeline.hpp>
#include <vivoka/vsdk/audio/PaStandalonePlayer.hpp>

// Global API
#include <vivoka/vsdk/global.hpp>
#include <vivoka/vsdk/Exception.hpp>
#include <vivoka/vsdk/Constants.hpp>

/**
 * @namespace Vivoka
 * @brief Vivoka VSDK Unified Interface
 */
namespace Vivoka {
    using namespace Vsdk;
}

#endif // VIVOKA_H
