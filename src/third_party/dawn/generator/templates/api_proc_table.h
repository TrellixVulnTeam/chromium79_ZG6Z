//* Copyright 2019 The Dawn Authors
//*
//* Licensed under the Apache License, Version 2.0 (the "License");
//* you may not use this file except in compliance with the License.
//* You may obtain a copy of the License at
//*
//*     http://www.apache.org/licenses/LICENSE-2.0
//*
//* Unless required by applicable law or agreed to in writing, software
//* distributed under the License is distributed on an "AS IS" BASIS,
//* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//* See the License for the specific language governing permissions and
//* limitations under the License.

#ifndef DAWN_DAWN_PROC_TABLE_H_
#define DAWN_DAWN_PROC_TABLE_H_

#include "dawn/dawn.h"

typedef struct DawnProcTable {
    DawnProcGetProcAddress getProcAddress;

    {% for type in by_category["object"] %}
        {% for method in native_methods(type) %}
            {{as_cProc(type.name, method.name)}} {{as_varName(type.name, method.name)}};
        {% endfor %}

    {% endfor %}
} DawnProcTable;

#endif  // DAWN_DAWN_PROC_TABLE_H_
