/*
 Copyright 2016 Nervana Systems Inc.
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#pragma once

#include "provider_interface.hpp"
#include "etl_audio.hpp"

namespace nervana
{
    class audio_only;
}

class nervana::audio_only : public provider_interface
{
public:
    audio_only(nlohmann::json js);
    void provide(int idx, buffer_in_array& in_buf, buffer_out_array& out_buf) override;

private:
    audio::config               audio_config;
    audio::extractor            audio_extractor;
    audio::transformer          audio_transformer;
    audio::loader               audio_loader;
    audio::param_factory        audio_factory;
};
