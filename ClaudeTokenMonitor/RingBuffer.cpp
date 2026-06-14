// RingBuffer.cpp — CRingBuffer<T, N> is a header-only template; this .cpp exists
// only because the vcxproj lists it as a ClCompile entry. No definitions here.
// Reference: plan §1 file list; RingBuffer.h (template definition)

#include "pch.h"
#include "RingBuffer.h"

// Intentionally empty. Template instantiations happen implicitly via DataManager.cpp
// which includes RingBuffer.h and references CRingBuffer<float, kHistoryCapacity>.