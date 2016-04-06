#include "breakpad.h"

#include <SDL_platform.h>

#ifdef USE_BREAKPAD
#include <stdio.h>

#ifdef __LINUX__
#include <breakpad/client/linux/handler/exception_handler.h>
#define BREAKPAD_PATH "/tmp"
#endif __LINUX__

#ifdef __WINDOWS__
#include <breakpad/client/windows/handler/exception_handler.h>
#define BREAKPAD_PATH "."
#endif __WINDOWS__

static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded) {
	  printf("Dump path: %s\n", descriptor.path());
	  return succeeded;
}

extern "C" CExceptionHandler newCExceptionHandlerSimple(void)
{
	printf("init Simple breakpad\n");
	// Path must exist and be RW!
	//google_breakpad::MinidumpDescriptor descriptor(BREAKPAD_PATH);
	//google_breakpad::ExceptionHandler eh(descriptor, NULL, dumpCallback, NULL, true, -1);
	return reinterpret_cast<void*>(new google_breakpad::ExceptionHandler(
	                                   L"dumps\\",
	                                   0,
	                                   0,
	                                   0,
	                                   google_breakpad::ExceptionHandler::HANDLER_ALL,
	                                   MiniDumpNormal,
	                                   L"",
	                                   0 ));
}


#endif // USE_BREAKPAD
