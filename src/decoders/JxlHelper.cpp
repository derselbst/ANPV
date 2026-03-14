
#include "JxlHelper.hpp"
#include "Formatter.hpp"

#include <stdexcept>
#include <thread>

namespace JxlHelper
{

Decoder::Decoder()
    : m_dec(JxlDecoderMake(nullptr))
{
    if(!m_dec)
    {
        throw std::runtime_error("JxlDecoderMake() failed");
    }
}

DecodedBlock Decoder::decodeCodestream(const uint8_t *data, size_t dataSize)
{
    JxlDecoderReset(m_dec.get());

    if(JxlDecoderSubscribeEvents(m_dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS)
    {
        throw std::runtime_error("JxlDecoderSubscribeEvents() failed");
    }

    if(JxlDecoderSetInput(m_dec.get(), data, dataSize) != JXL_DEC_SUCCESS)
    {
        throw std::runtime_error("JxlDecoderSetInput() failed");
    }

    JxlDecoderCloseInput(m_dec.get());

    DecodedBlock result;

    for(;;)
    {
        JxlDecoderStatus status = JxlDecoderProcessInput(m_dec.get());

        switch(status)
        {
        case JXL_DEC_BASIC_INFO:
        {
            JxlBasicInfo info;

            if(JxlDecoderGetBasicInfo(m_dec.get(), &info) != JXL_DEC_SUCCESS)
            {
                throw std::runtime_error("JxlDecoderGetBasicInfo() failed");
            }

            result.width = info.xsize;
            result.height = info.ysize;
            break;
        }

        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
        {
            size_t buffer_size;

            if(JxlDecoderImageOutBufferSize(m_dec.get(), &jxlFormat, &buffer_size) != JXL_DEC_SUCCESS)
            {
                throw std::runtime_error("JxlDecoderImageOutBufferSize() failed");
            }

            result.pixels.resize(buffer_size);

            if(JxlDecoderSetImageOutBuffer(m_dec.get(), &jxlFormat, result.pixels.data(), result.pixels.size()) != JXL_DEC_SUCCESS)
            {
                throw std::runtime_error("JxlDecoderSetImageOutBuffer() failed");
            }

            break;
        }

        case JXL_DEC_FULL_IMAGE:
        case JXL_DEC_SUCCESS:
            return result;

        case JXL_DEC_ERROR:
            throw std::runtime_error("JXL decoder error");

        default:
            throw std::runtime_error(Formatter() << "Unexpected JXL decoder status: " << status);
        }
    }
}

DecoderPool::DecoderPool(size_t numWorkers)
{
    if(numWorkers == 0)
    {
        numWorkers = std::max(1u, std::thread::hardware_concurrency());
    }

    m_decoders.reserve(numWorkers);
    m_threads.reserve(numWorkers);

    for(size_t i = 0; i < numWorkers; i++)
    {
        m_decoders.push_back(std::make_unique<Decoder>());
        // Pass a reference to the just-added decoder; reserve() guarantees no reallocation.
        m_threads.emplace_back(&DecoderPool::workerFunc, this, std::ref(*m_decoders.back()));
    }
}

DecoderPool::~DecoderPool()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }

    m_cv.notify_all();

    for(auto &t : m_threads)
    {
        t.join();
    }
}

std::future<void> DecoderPool::submit(std::vector<uint8_t> rawData,
                                      std::function<void(DecodedBlock &&)> postProcess,
                                      std::atomic<bool> *cancelFlag)
{
    Task task;
    task.rawData = std::move(rawData);
    task.postProcess = std::move(postProcess);
    task.cancelFlag = cancelFlag;
    std::future<void> future = task.result.get_future();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taskQueue.push_back(std::move(task));
    }

    m_cv.notify_one();
    return future;
}

void DecoderPool::workerFunc(Decoder &decoder)
{
    while(true)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_taskQueue.empty() || m_stopping; });

            if(m_taskQueue.empty())
            {
                // m_stopping is true and queue is drained — exit
                break;
            }

            task = std::move(m_taskQueue.front());
            m_taskQueue.pop_front();
        }

        try
        {
            // Check cancellation before decoding
            if(task.cancelFlag && task.cancelFlag->load(std::memory_order_acquire))
            {
                task.result.set_value();
                continue;
            }

            DecodedBlock decoded = decoder.decodeCodestream(task.rawData.data(), task.rawData.size());

            // Check cancellation before writing to the output buffer
            const bool isCancelled = task.cancelFlag && task.cancelFlag->load(std::memory_order_acquire);
            if(!isCancelled)
            {
                task.postProcess(std::move(decoded));
            }

            task.result.set_value();
        }
        catch(...)
        {
            task.result.set_exception(std::current_exception());
        }
    }
}

void convertRGBAtoARGB32(uint32_t *__restrict dst, const uint8_t *__restrict src, uint32_t count)
{
    for(uint32_t i = 0; i < count; i++)
    {
        uint8_t r = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];
        dst[i] = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
}

} // namespace JxlHelper
