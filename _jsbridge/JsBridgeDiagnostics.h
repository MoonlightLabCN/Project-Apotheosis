#pragma once

// Synchronous native breadcrumbs, including WTF assertion diagnostics.
// Implemented in JsBridge.cpp; never calls back into managed code.
void JsBridgeTrace(const char* reason);
