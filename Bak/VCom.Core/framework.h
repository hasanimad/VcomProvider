#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include <combaseapi.h>
#include <SetupAPI.h>
#include <WinSock2.h>
#include <devguid.h>
#include <WS2tcpip.h>
#include <string>
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include "json.hpp"

