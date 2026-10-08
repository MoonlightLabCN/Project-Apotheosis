// ============================================================================
//  PortMediaAudio.h — Apotheosis media audio bridge (see docs/MEDIA-MF-IMPLEMENTATION.md)
//
//  WebCore's WinUWP media backend decodes audio but owns no device. This file is the
//  platform half of that contract: it installs a WinUWPMediaAudioSink backed by
//  PortAudioOutput, and it is what the harness pulls PCM from through the driver's
//  C ABI.
//
//  Why the device object itself lives in the harness and not here:
//    the queue is plain memory and safe to touch from the clang-cl WebCore driver, but
//    obtaining an IAudioClient in an App Container means WinRT activation
//    (ActivateAudioInterfaceAsync / AudioGraph), which is C++/CX territory and belongs
//    to the MSVC-built harness. So the driver owns the buffer and the harness owns the
//    device, exactly as the port already splits presentation between them.
// ============================================================================

#pragma once

namespace WebCorePort {

// Installs the sink into WebCore. Idempotent, and a no-op if WebCore was built without
// media support. Called once when the engine session is built, before any page can
// create a <video>, so no decode thread can ever race the install.
void installMediaAudioSink();

// --- pull side (harness audio worker) ---
// Copies up to `frames` interleaved float32 frames into `out`, zero-filling whatever is
// missing and setting *outUnderran when the queue ran dry. Returns the frames that were
// actually available. Never blocks on the decode thread: the queue is a plain ring.
int takeMediaPCM(float* out, int frames, bool* outUnderran);

// 0 until the first stream configures; the harness uses this to build its IAudioClient
// in the right format.
int mediaAudioSampleRate();
int mediaAudioChannels();

// --- diagnostics (stage.txt) ---
// Writes a one-line summary of the media path (queue depth, pushed/consumed/dropped
// frames, decoded/presented video frames, last Media Foundation failure and the
// negotiated video subtypes). Always NUL-terminates; degrades to "" if it cannot.
void appendMediaDiagnostics(char* out, int outBytes);

} // namespace WebCorePort
