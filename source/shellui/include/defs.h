/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once

#include <onion/version.h>

#define PUBLIC_TEST 0
#define PRE_RELEASE 0

/*
 * Guards ~50 verbose diagnostic blocks in the ShellUI hooks. It was hardcoded
 * to 1, so those blocks shipped in release payloads — ShellUI is an injected
 * system process, and the chattiest logs in the project ran on every boot.
 *
 * Follows the build type via NDEBUG; override with -DSHELL_DEBUG=1 to get the
 * verbose blocks in a release build when chasing a report.
 */
#ifndef SHELL_DEBUG
#ifdef NDEBUG
#define SHELL_DEBUG 0
#else
#define SHELL_DEBUG 1
#endif
#endif

#define libSceKernelHandle 0x2001
#define KERNEL_DLSYM(handle, sym) \
    (*(void**)&sym=(void*)kernel_dynlib_dlsym(-1, handle, #sym))


typedef void* ScePthread;
