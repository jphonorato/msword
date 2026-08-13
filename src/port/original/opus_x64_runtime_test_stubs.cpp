/*
 * Link-time stub for opus_x64_runtime_test.
 *
 * OpusX64TraceRibbon is defined for real in opus_original_startup_probe.cpp
 * (compiled directly into WORD1/opus_original_startup_probe, not into the
 * opus_x64_runtime static library) and merely declared+called from
 * opus_sdm_runtime.cpp, which *is* part of opus_x64_runtime. Any binary that
 * links libopus_x64_runtime.a without also linking the startup probe needs
 * its own definition to satisfy the reference. opus_x64_runtime_test is
 * exactly that case: a unit test for the runtime layer with no interest in
 * the probe's real ribbon tracing, so a no-op is correct here.
 */
extern "C" void OpusX64TraceRibbon(const char*, int, int, int, int, long, long, int) {
}
