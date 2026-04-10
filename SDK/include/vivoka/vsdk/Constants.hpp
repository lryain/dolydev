/// @file      Constants.hpp
/// @author    Pierre Caissial
/// @date      Created on 14/01/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

#define JSON_KEY_VALUE(key, value) constexpr static auto const key = value
#define JSON_KEY(key)              JSON_KEY_VALUE(key, #key)

namespace Vsdk { namespace Constants
{
    namespace AsrResult
    {
        JSON_KEY(confidence);
        JSON_KEY(hypotheses);
        JSON_KEY(items);
        JSON_KEY(name);
        JSON_KEY(orthography);
        JSON_KEY(type);

        JSON_KEY_VALUE(activeStartRules, "active_start_rules");
        JSON_KEY_VALUE(beginTime,        "begin_time");
        JSON_KEY_VALUE(endTime,          "end_time");
        JSON_KEY_VALUE(startRule,        "start_rule");
        JSON_KEY_VALUE(typeTerminal,     "terminal");
        JSON_KEY_VALUE(typeTag,          "tag");
    } // !namespace AsrResult

    namespace Config
    {
        JSON_KEY(afe);
        JSON_KEY(asr);
        JSON_KEY(biometrics);
        JSON_KEY(nlu);
        JSON_KEY(paths);
        JSON_KEY(settings);
        JSON_KEY(tts);
        JSON_KEY(version);

        JSON_KEY_VALUE(speechEnhancement, "speech_enhancement");

        namespace Asr
        {
            JSON_KEY(models);
            JSON_KEY(recognizers);

            namespace Model
            {
                JSON_KEY(file);
                JSON_KEY(language);
                JSON_KEY(slots);
                JSON_KEY(type);

                JSON_KEY_VALUE(typeDictation,  "dictation");
                JSON_KEY_VALUE(typeDynamic,    "dynamic");
                JSON_KEY_VALUE(typeFreeSpeech, "free-speech");
                JSON_KEY_VALUE(typeStatic,     "static");
            } // !namespace Model
        } // !namespace Asr

        namespace Paths
        {
            JSON_KEY_VALUE(dataRoot, "data_root");
        } // !namespace Paths

        namespace SpeechEnhancement
        {
            JSON_KEY_VALUE(speechEnhancers, "speech_enhancers");
        } // !namespace SpeechEnhancement

        namespace Tts
        {
            JSON_KEY(channels);
            JSON_KEY(voices);
        } // !namespace Tts

        namespace Nlu
        {
            JSON_KEY(model);
            JSON_KEY(parsers);
        } // !namespace Nlu
    } // !namespace Config

    namespace FileSystemErrors
    {
        JSON_KEY_VALUE(notADir,  "not a directory");
        JSON_KEY_VALUE(notAFile, "not a file");
        JSON_KEY_VALUE(notFound, "no such file or directory");
    } // !namespace FileSystemErrors

    namespace NluResult
    {
        JSON_KEY(captures);
        JSON_KEY(entities);
        JSON_KEY(hypotheses);
        JSON_KEY(intent);
        JSON_KEY(lang);

        JSON_KEY_VALUE(beginTime, "begin_time");
        JSON_KEY_VALUE(originalSentence, "original_sentence");

        namespace Intent
        {
            JSON_KEY(confidence);
            JSON_KEY(name);
        } // !namespace Intent

        namespace Entity
        {
            JSON_KEY(confidence);
            JSON_KEY(name);
            JSON_KEY(value);

            JSON_KEY_VALUE(endIndex, "end_index");
            JSON_KEY_VALUE(startIndex, "start_index");
        } // !namespace Entity
    } // !namespace NluResult

    namespace Tts
    {
        namespace Events
        {
            namespace Marker
            {
                JSON_KEY(index);
                JSON_KEY(name);

                JSON_KEY_VALUE(posInAudio, "pos_in_audio");
                JSON_KEY_VALUE(posInText,  "pos_in_text");
            } // !namespace Marker

            namespace WordMarker
            {
                JSON_KEY(index);
                JSON_KEY(text);
                JSON_KEY(word);

                JSON_KEY_VALUE(endPosInAudio,   "end_pos_in_audio");
                JSON_KEY_VALUE(endPosInText,    "end_pos_in_text");
                JSON_KEY_VALUE(startPosInAudio, "start_pos_in_audio");
                JSON_KEY_VALUE(startPosInText,  "start_pos_in_text");
            } // !namespace WordMarker
        } // !namespace Events
    } // !namespace Tts
}} // !namespace Vsdk::Constants
