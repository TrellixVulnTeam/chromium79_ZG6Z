@echo off
:: Copyright (c) 2012 The Chromium Authors. All rights reserved.
:: Use of this source code is governed by a BSD-style license that can be
:: found in the LICENSE file.
setlocal

:: Shall skip automatic update?
IF "%DEPOT_TOOLS_UPDATE%" == "0" GOTO :CALL_GCLIENT

:: Synchronize the root directory before deferring control back to gclient.py.
call "%~dp0update_depot_tools.bat" %*

:CALL_GCLIENT
:: Ensure that "depot_tools" is somewhere in PATH so this tool can be used
:: standalone, but allow other PATH manipulations to take priority.
set PATH=%PATH%;%~dp0

:: Defer control.
IF "%GCLIENT_PY3%" == "1" (
  :: TODO(1003139): Use vpython3 once vpython3 works on Windows.
  python3 "%~dp0gclient.py" %*
) ELSE (
  python "%~dp0gclient.py" %*
)
