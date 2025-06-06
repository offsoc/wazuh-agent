#include <task_manager.hpp>

#include <logger.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>

#include <utility>

TaskManager::~TaskManager()
{
    Stop();
}

void TaskManager::StartThreadPool(size_t numThreads)
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_threads.empty())
    {
        LogWarn("Task manager thread pool already started");
        return;
    }

    if (m_ioContext.stopped())
    {
        m_ioContext.restart();
    }

    if (!m_work)
    {
        m_work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            boost::asio::make_work_guard(m_ioContext));
    }

    for (size_t i = 0; i < numThreads; ++i)
    {
        m_threads.emplace_back([this]() { m_ioContext.run(); });
    }
}

void TaskManager::RunSingleThread()
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_work)
        {
            m_work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
                boost::asio::make_work_guard(m_ioContext));
        }

        if (m_ioContext.stopped())
        {
            m_ioContext.restart();
        }
    }

    ++m_numAdditionalThreads;
    m_ioContext.run();
    --m_numAdditionalThreads;
}

void TaskManager::Stop()
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    if (m_work)
    {
        m_work.reset();
    }

    if (!m_ioContext.stopped())
    {
        m_ioContext.stop();
    }

    if (!m_threads.empty())
    {
        for (std::thread& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        m_threads.clear();
        m_numEnqueuedThreadTasks = 0;
    }
}

void TaskManager::EnqueueTask(std::function<void()> task, const std::string& taskID)
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    if (++m_numEnqueuedThreadTasks > GetNumThreads())
    {
        LogError("Enqueued more threaded tasks than available threads");
    }

    // NOLINTBEGIN(bugprone-exception-escape)
    auto taskWithThreadCounter = [this, taskID, task = std::move(task)]() mutable
    {
        try
        {
            task();
        }
        catch (const std::exception& e)
        {
            LogError("{} task exited with an exception: {}", taskID.empty() ? "Anonymous" : taskID, e.what());
        }
        --m_numEnqueuedThreadTasks;
    };
    // NOLINTEND(bugprone-exception-escape)

    boost::asio::post(m_ioContext, std::move(taskWithThreadCounter));
}

void TaskManager::EnqueueTask(boost::asio::awaitable<void> task, const std::string& taskID)
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    // NOLINTBEGIN(bugprone-exception-escape, performance-unnecessary-value-param)
    boost::asio::co_spawn(m_ioContext,
                          std::move(task),
                          [taskID](std::exception_ptr ep)
                          {
                              if (ep)
                              {
                                  try
                                  {
                                      std::rethrow_exception(ep);
                                  }
                                  catch (const std::exception& e)
                                  {
                                      LogError("{} coroutine task exited with an exception: {}",
                                               taskID.empty() ? "Anonymous" : taskID,
                                               e.what());
                                  }
                              }
                          });
    // NOLINTEND(bugprone-exception-escape, performance-unnecessary-value-param)
}

size_t TaskManager::GetNumEnqueuedThreadTasks() const
{
    return m_numEnqueuedThreadTasks;
}

size_t TaskManager::GetNumThreads() const
{
    return m_threads.size() + m_numAdditionalThreads;
}

bool TaskManager::IsStopped() const
{
    return GetNumThreads() == 0;
}

boost::asio::steady_timer TaskManager::CreateSteadyTimer(std::chrono::milliseconds ms)
{
    return boost::asio::steady_timer(m_ioContext, ms);
}
