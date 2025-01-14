// GENERATED FILE - DO NOT EDIT.
// Generated by generate_entry_points.py using data from gl.xml.
//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// entry_points_gl_3_1_autogen.cpp:
//   Defines the GL 3.1 entry points.

#include "libGL/entry_points_gl_3_1_autogen.h"

#include "libANGLE/Context.h"
#include "libANGLE/Context.inl.h"
#include "libANGLE/entry_points_utils.h"
#include "libANGLE/gl_enum_utils_autogen.h"
#include "libANGLE/validationEGL.h"
#include "libANGLE/validationES.h"
#include "libANGLE/validationES1.h"
#include "libANGLE/validationES2.h"
#include "libANGLE/validationES3.h"
#include "libANGLE/validationES31.h"
#include "libANGLE/validationES32.h"
#include "libANGLE/validationESEXT.h"
#include "libANGLE/validationGL31_autogen.h"
#include "libGLESv2/global_state.h"

namespace gl
{
void GL_APIENTRY CopyBufferSubData(GLenum readTarget,
                                   GLenum writeTarget,
                                   GLintptr readOffset,
                                   GLintptr writeOffset,
                                   GLsizeiptr size)
{
    Context *context = GetValidGlobalContext();
    EVENT("glCopyBufferSubData",
          "context = %d, GLenum readTarget = %s, GLenum writeTarget = %s, GLintptr readOffset = "
          "%llu, GLintptr writeOffset = %llu, GLsizeiptr size = %llu",
          CID(context), GLenumToString(GLenumGroup::CopyBufferSubDataTarget, readTarget),
          GLenumToString(GLenumGroup::CopyBufferSubDataTarget, writeTarget),
          static_cast<unsigned long long>(readOffset), static_cast<unsigned long long>(writeOffset),
          static_cast<unsigned long long>(size));

    if (context)
    {
        BufferBinding readTargetPacked                = FromGL<BufferBinding>(readTarget);
        BufferBinding writeTargetPacked               = FromGL<BufferBinding>(writeTarget);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateCopyBufferSubData(context, readTargetPacked, writeTargetPacked,
                                                      readOffset, writeOffset, size));
        if (isCallValid)
        {
            context->copyBufferSubData(readTargetPacked, writeTargetPacked, readOffset, writeOffset,
                                       size);
        }
        ANGLE_CAPTURE(CopyBufferSubData, isCallValid, context, readTargetPacked, writeTargetPacked,
                      readOffset, writeOffset, size);
    }
}

void GL_APIENTRY DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount)
{
    Context *context = GetValidGlobalContext();
    EVENT("glDrawArraysInstanced",
          "context = %d, GLenum mode = %s, GLint first = %d, GLsizei count = %d, GLsizei "
          "instancecount = %d",
          CID(context), GLenumToString(GLenumGroup::PrimitiveType, mode), first, count,
          instancecount);

    if (context)
    {
        PrimitiveMode modePacked                      = FromGL<PrimitiveMode>(mode);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid =
            (context->skipValidation() ||
             ValidateDrawArraysInstanced(context, modePacked, first, count, instancecount));
        if (isCallValid)
        {
            context->drawArraysInstanced(modePacked, first, count, instancecount);
        }
        ANGLE_CAPTURE(DrawArraysInstanced, isCallValid, context, modePacked, first, count,
                      instancecount);
    }
}

void GL_APIENTRY DrawElementsInstanced(GLenum mode,
                                       GLsizei count,
                                       GLenum type,
                                       const void *indices,
                                       GLsizei instancecount)
{
    Context *context = GetValidGlobalContext();
    EVENT("glDrawElementsInstanced",
          "context = %d, GLenum mode = %s, GLsizei count = %d, GLenum type = %s, const void "
          "*indices = 0x%016" PRIxPTR ", GLsizei instancecount = %d",
          CID(context), GLenumToString(GLenumGroup::PrimitiveType, mode), count,
          GLenumToString(GLenumGroup::DrawElementsType, type), (uintptr_t)indices, instancecount);

    if (context)
    {
        PrimitiveMode modePacked                      = FromGL<PrimitiveMode>(mode);
        DrawElementsType typePacked                   = FromGL<DrawElementsType>(type);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateDrawElementsInstanced(context, modePacked, count, typePacked,
                                                          indices, instancecount));
        if (isCallValid)
        {
            context->drawElementsInstanced(modePacked, count, typePacked, indices, instancecount);
        }
        ANGLE_CAPTURE(DrawElementsInstanced, isCallValid, context, modePacked, count, typePacked,
                      indices, instancecount);
    }
}

void GL_APIENTRY GetActiveUniformBlockName(GLuint program,
                                           GLuint uniformBlockIndex,
                                           GLsizei bufSize,
                                           GLsizei *length,
                                           GLchar *uniformBlockName)
{
    Context *context = GetValidGlobalContext();
    EVENT("glGetActiveUniformBlockName",
          "context = %d, GLuint program = %u, GLuint uniformBlockIndex = %u, GLsizei bufSize = %d, "
          "GLsizei *length = 0x%016" PRIxPTR ", GLchar *uniformBlockName = 0x%016" PRIxPTR "",
          CID(context), program, uniformBlockIndex, bufSize, (uintptr_t)length,
          (uintptr_t)uniformBlockName);

    if (context)
    {
        ShaderProgramID programPacked                 = FromGL<ShaderProgramID>(program);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid =
            (context->skipValidation() ||
             ValidateGetActiveUniformBlockName(context, programPacked, uniformBlockIndex, bufSize,
                                               length, uniformBlockName));
        if (isCallValid)
        {
            context->getActiveUniformBlockName(programPacked, uniformBlockIndex, bufSize, length,
                                               uniformBlockName);
        }
        ANGLE_CAPTURE(GetActiveUniformBlockName, isCallValid, context, programPacked,
                      uniformBlockIndex, bufSize, length, uniformBlockName);
    }
}

void GL_APIENTRY GetActiveUniformBlockiv(GLuint program,
                                         GLuint uniformBlockIndex,
                                         GLenum pname,
                                         GLint *params)
{
    Context *context = GetValidGlobalContext();
    EVENT("glGetActiveUniformBlockiv",
          "context = %d, GLuint program = %u, GLuint uniformBlockIndex = %u, GLenum pname = %s, "
          "GLint *params = 0x%016" PRIxPTR "",
          CID(context), program, uniformBlockIndex,
          GLenumToString(GLenumGroup::UniformBlockPName, pname), (uintptr_t)params);

    if (context)
    {
        ShaderProgramID programPacked                 = FromGL<ShaderProgramID>(program);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateGetActiveUniformBlockiv(context, programPacked,
                                                            uniformBlockIndex, pname, params));
        if (isCallValid)
        {
            context->getActiveUniformBlockiv(programPacked, uniformBlockIndex, pname, params);
        }
        ANGLE_CAPTURE(GetActiveUniformBlockiv, isCallValid, context, programPacked,
                      uniformBlockIndex, pname, params);
    }
}

void GL_APIENTRY GetActiveUniformName(GLuint program,
                                      GLuint uniformIndex,
                                      GLsizei bufSize,
                                      GLsizei *length,
                                      GLchar *uniformName)
{
    Context *context = GetValidGlobalContext();
    EVENT("glGetActiveUniformName",
          "context = %d, GLuint program = %u, GLuint uniformIndex = %u, GLsizei bufSize = %d, "
          "GLsizei *length = 0x%016" PRIxPTR ", GLchar *uniformName = 0x%016" PRIxPTR "",
          CID(context), program, uniformIndex, bufSize, (uintptr_t)length, (uintptr_t)uniformName);

    if (context)
    {
        ShaderProgramID programPacked                 = FromGL<ShaderProgramID>(program);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateGetActiveUniformName(context, programPacked, uniformIndex,
                                                         bufSize, length, uniformName));
        if (isCallValid)
        {
            context->getActiveUniformName(programPacked, uniformIndex, bufSize, length,
                                          uniformName);
        }
        ANGLE_CAPTURE(GetActiveUniformName, isCallValid, context, programPacked, uniformIndex,
                      bufSize, length, uniformName);
    }
}

void GL_APIENTRY GetActiveUniformsiv(GLuint program,
                                     GLsizei uniformCount,
                                     const GLuint *uniformIndices,
                                     GLenum pname,
                                     GLint *params)
{
    Context *context = GetValidGlobalContext();
    EVENT("glGetActiveUniformsiv",
          "context = %d, GLuint program = %u, GLsizei uniformCount = %d, const GLuint "
          "*uniformIndices = 0x%016" PRIxPTR ", GLenum pname = %s, GLint *params = 0x%016" PRIxPTR
          "",
          CID(context), program, uniformCount, (uintptr_t)uniformIndices,
          GLenumToString(GLenumGroup::UniformPName, pname), (uintptr_t)params);

    if (context)
    {
        ShaderProgramID programPacked                 = FromGL<ShaderProgramID>(program);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateGetActiveUniformsiv(context, programPacked, uniformCount,
                                                        uniformIndices, pname, params));
        if (isCallValid)
        {
            context->getActiveUniformsiv(programPacked, uniformCount, uniformIndices, pname,
                                         params);
        }
        ANGLE_CAPTURE(GetActiveUniformsiv, isCallValid, context, programPacked, uniformCount,
                      uniformIndices, pname, params);
    }
}

GLuint GL_APIENTRY GetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName)
{
    Context *context = GetValidGlobalContext();
    EVENT("glGetUniformBlockIndex",
          "context = %d, GLuint program = %u, const GLchar *uniformBlockName = 0x%016" PRIxPTR "",
          CID(context), program, (uintptr_t)uniformBlockName);

    GLuint returnValue;
    if (context)
    {
        ShaderProgramID programPacked                 = FromGL<ShaderProgramID>(program);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateGetUniformBlockIndex(context, programPacked, uniformBlockName));
        if (isCallValid)
        {
            returnValue = context->getUniformBlockIndex(programPacked, uniformBlockName);
        }
        else
        {
            returnValue = GetDefaultReturnValue<EntryPoint::GetUniformBlockIndex, GLuint>();
        }
        ANGLE_CAPTURE(GetUniformBlockIndex, isCallValid, context, programPacked, uniformBlockName,
                      returnValue);
    }
    else
    {
        returnValue = GetDefaultReturnValue<EntryPoint::GetUniformBlockIndex, GLuint>();
    }
    return returnValue;
}

void GL_APIENTRY GetUniformIndices(GLuint program,
                                   GLsizei uniformCount,
                                   const GLchar *const *uniformNames,
                                   GLuint *uniformIndices)
{
    Context *context = GetValidGlobalContext();
    EVENT("glGetUniformIndices",
          "context = %d, GLuint program = %u, GLsizei uniformCount = %d, const GLchar "
          "*const*uniformNames = 0x%016" PRIxPTR ", GLuint *uniformIndices = 0x%016" PRIxPTR "",
          CID(context), program, uniformCount, (uintptr_t)uniformNames, (uintptr_t)uniformIndices);

    if (context)
    {
        ShaderProgramID programPacked                 = FromGL<ShaderProgramID>(program);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateGetUniformIndices(context, programPacked, uniformCount,
                                                      uniformNames, uniformIndices));
        if (isCallValid)
        {
            context->getUniformIndices(programPacked, uniformCount, uniformNames, uniformIndices);
        }
        ANGLE_CAPTURE(GetUniformIndices, isCallValid, context, programPacked, uniformCount,
                      uniformNames, uniformIndices);
    }
}

void GL_APIENTRY PrimitiveRestartIndex(GLuint index)
{
    Context *context = GetValidGlobalContext();
    EVENT("glPrimitiveRestartIndex", "context = %d, GLuint index = %u", CID(context), index);

    if (context)
    {
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid =
            (context->skipValidation() || ValidatePrimitiveRestartIndex(context, index));
        if (isCallValid)
        {
            context->primitiveRestartIndex(index);
        }
        ANGLE_CAPTURE(PrimitiveRestartIndex, isCallValid, context, index);
    }
}

void GL_APIENTRY TexBuffer(GLenum target, GLenum internalformat, GLuint buffer)
{
    Context *context = GetValidGlobalContext();
    EVENT("glTexBuffer",
          "context = %d, GLenum target = %s, GLenum internalformat = %s, GLuint buffer = %u",
          CID(context), GLenumToString(GLenumGroup::TextureTarget, target),
          GLenumToString(GLenumGroup::InternalFormat, internalformat), buffer);

    if (context)
    {
        BufferID bufferPacked                         = FromGL<BufferID>(buffer);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateTexBuffer(context, target, internalformat, bufferPacked));
        if (isCallValid)
        {
            context->texBuffer(target, internalformat, bufferPacked);
        }
        ANGLE_CAPTURE(TexBuffer, isCallValid, context, target, internalformat, bufferPacked);
    }
}

void GL_APIENTRY UniformBlockBinding(GLuint program,
                                     GLuint uniformBlockIndex,
                                     GLuint uniformBlockBinding)
{
    Context *context = GetValidGlobalContext();
    EVENT("glUniformBlockBinding",
          "context = %d, GLuint program = %u, GLuint uniformBlockIndex = %u, GLuint "
          "uniformBlockBinding = %u",
          CID(context), program, uniformBlockIndex, uniformBlockBinding);

    if (context)
    {
        ShaderProgramID programPacked                 = FromGL<ShaderProgramID>(program);
        std::unique_lock<std::mutex> shareContextLock = GetShareGroupLock(context);
        bool isCallValid                              = (context->skipValidation() ||
                            ValidateUniformBlockBinding(context, programPacked, uniformBlockIndex,
                                                        uniformBlockBinding));
        if (isCallValid)
        {
            context->uniformBlockBinding(programPacked, uniformBlockIndex, uniformBlockBinding);
        }
        ANGLE_CAPTURE(UniformBlockBinding, isCallValid, context, programPacked, uniformBlockIndex,
                      uniformBlockBinding);
    }
}
}  // namespace gl
