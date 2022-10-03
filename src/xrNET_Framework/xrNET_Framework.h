#pragma once

#ifdef XRNETFRAMEWORK_EXPORTS
#define XRNETFRAMEWORK_API _declspec(dllexport)
#else
#define XRNETFRAMEWORK_API _declspec(dllimport)
#endif

XRNETFRAMEWORK_API bool HasWebFile(const char* web_path);