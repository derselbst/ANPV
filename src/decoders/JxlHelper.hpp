
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <deque>
#include <future>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>

namespace JxlHelper
{
    // Shared RGBA uint8 pixel format used by all JXL decoders in ANPV
    inline constexpr JxlPixelFormat jxlFormat = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };

    // Result of a single JXL codestream decode.
    struct DecodedBlock
    {
        std::vector<uint8_t> pixels; // RGBA uint8 pixels (width * height * 4 bytes)
        uint32_t width = 0;
        uint32_t height = 0;
    };

    // Single-threaded reusable JXL decoder.
    // Intended to be owned by one worker thread and reused across decode calls.
    class Decoder
    {
    public:
        Decoder();

        Decoder(const Decoder &) = delete;
        Decoder &operator=(const Decoder &) = delete;

        // Decode a JXL codestream to RGBA uint8 pixels (top-down row order).
        // The decoder instance is reset automatically between calls.
        DecodedBlock decodeCodestream(const uint8_t *data, size_t dataSize);

    private:
        JxlDecoderPtr m_dec;
    };

    // Thread pool of single-threaded JXL decoders for producer/consumer parallel decoding.
    //
    // Usage pattern:
    //   - One producer thread reads raw compressed data (e.g. from libtiff) and calls submit().
    //   - Worker threads owned by the pool decode in parallel, each with its own Decoder.
    //     After decoding, the worker calls postProcess() with the decoded block.
    //   - The producer collects std::future<void> objects and waits for them in the consumer.
    //
    // Cancellation:
    //   - Pass a pointer to a shared std::atomic<bool> cancelFlag to submit().
    //   - Workers check the flag before decoding and before calling postProcess().
    //   - If the flag is set, the task is skipped and the future is resolved normally.
    //   - The consumer should set cancelFlag = true and drain all pending futures before
    //     propagating any exception (to ensure no worker is writing to a buffer being freed).
    class DecoderPool
    {
    public:
        // numWorkers == 0 uses std::thread::hardware_concurrency() (at least 1).
        explicit DecoderPool(size_t numWorkers = 0);
        ~DecoderPool();

        DecoderPool(const DecoderPool &) = delete;
        DecoderPool &operator=(const DecoderPool &) = delete;

        // Submit a raw JXL codestream for asynchronous decode + post-processing.
        // - rawData is moved into the task.
        // - postProcess is called by the worker thread after a successful decode;
        //   it receives the decoded block and should write pixels to the output buffer.
        // - cancelFlag: if non-null and set to true before/after decoding, postProcess
        //   is skipped and the future resolves without calling postProcess.
        // Returns a future<void> that resolves when the task completes (or is skipped).
        std::future<void> submit(std::vector<uint8_t> rawData,
                                 std::function<void(DecodedBlock &&)> postProcess,
                                 std::atomic<bool> *cancelFlag = nullptr);

    private:
        struct Task
        {
            std::vector<uint8_t> rawData;
            std::function<void(DecodedBlock &&)> postProcess;
            std::atomic<bool> *cancelFlag = nullptr;
            std::promise<void> result;
        };

        void workerFunc(Decoder &decoder);

        std::vector<std::unique_ptr<Decoder>> m_decoders;
        std::vector<std::thread> m_threads;
        std::deque<Task> m_taskQueue;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_stopping = false;
    };

    // Convert RGBA byte-ordered pixels to ARGB32 (0xAARRGGBB) layout.
    void convertRGBAtoARGB32(uint32_t *__restrict dst, const uint8_t *__restrict src, uint32_t count);
}
