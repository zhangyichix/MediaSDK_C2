/********************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2017-2019 Intel Corporation. All Rights Reserved.

*********************************************************************************/

#pragma once

#include "mfx_debug.h"
#include <C2Param.h>
#include <C2Work.h>
#include <map>

class MfxC2ParamReflector : public C2ParamReflector
{
private:
    std::vector<std::shared_ptr<C2ParamDescriptor>> params_descriptors_;

    std::map<C2Param::Type, C2StructDescriptor> params_struct_descriptors_;

    std::map<C2ParamField, C2FieldSupportedValues> params_supported_values_;

protected:
    std::unique_ptr<C2StructDescriptor> describe(
        C2Param::CoreIndex coreIndex) const override;

public:
    template<typename ParamType>
    void RegisterParam(const char* param_name);

    template<typename ParamType, typename ValueType, typename FieldType>
    void RegisterSupportedRange(FieldType ValueType::* pm, FieldType min, FieldType max);

    bool ValidateParam(const C2Param* param,
        std::vector<std::unique_ptr<C2SettingResult>>* const failures);

    std::unique_ptr<C2SettingResult> FindParam(
        const C2Param* param) const;

    bool FindParam(C2Param::Index param_index) const;

    c2_status_t getSupportedParams(
        std::vector<std::shared_ptr<C2ParamDescriptor>>* const params) const;

#if MFX_DEBUG == MFX_DEBUG_YES
    void DumpParams();
#else
    void DumpParams() { }
#endif
};

template<typename ParamType>
void MfxC2ParamReflector::RegisterParam(const char* param_name)
{
    using namespace android;

    params_descriptors_.push_back(
        std::make_shared<C2ParamDescriptor>(false, param_name, ParamType::PARAM_TYPE));

    params_struct_descriptors_.insert({ ParamType::PARAM_TYPE, C2StructDescriptor(ParamType::PARAM_TYPE, ParamType::FieldList()) });
};

template<typename ParamType, typename ValueType, typename FieldType>
void MfxC2ParamReflector::RegisterSupportedRange(FieldType ValueType::* pm, FieldType min, FieldType max)
{
    using namespace android;

    ParamType temp_param; // C2ParamField constructor demands pointer to instance

    C2ParamField field(&temp_param, pm);
    C2FieldSupportedValues values(min, max);

    params_supported_values_.emplace(field, values);
};
