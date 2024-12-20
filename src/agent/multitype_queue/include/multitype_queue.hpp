#pragma once

#include <imultitype_queue.hpp>
#include <message.hpp>
#include <persistence.hpp>

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio.hpp>

// TODO: move to a configuration setting
constexpr int DEFAULT_MAX = 10000;
constexpr int DEFAULT_TIMEOUT_S = 3;
const std::string QUEUE_DB_NAME = "queue.db";

/**
 * @brief MultiTypeQueue implementation that handles multiple types of messages.
 *
 * This class implements the IMultiTypeQueue interface to provide a queue
 * that can handle different message types such as STATELESS, STATEFUL, and COMMAND.
 */
class MultiTypeQueue : public IMultiTypeQueue
{
private:
    const std::vector<std::string> m_vMessageTypeStrings {"STATELESS", "STATEFUL", "COMMAND"};
    const std::map<MessageType, std::string> m_mapMessageTypeName {
        {MessageType::STATELESS, "STATELESS"},
        {MessageType::STATEFUL, "STATEFUL"},
        {MessageType::COMMAND, "COMMAND"},
    };
    const size_t m_maxItems;
    const std::chrono::seconds m_timeout;
    std::unique_ptr<Persistence> m_persistenceDest;
    std::mutex m_mtx;
    std::condition_variable m_cv;

public:
    /**
     * @brief Constructor.
     *
     * @param dbFolderPath The path to the database folder.
     * @param size The maximum number of items in the queue.
     * @param timeout The timeout period in seconds.
     */
    MultiTypeQueue(const std::string& dbFolderPath, size_t size = DEFAULT_MAX, int timeout = DEFAULT_TIMEOUT_S);

    /**
     * @brief Delete copy constructor
     */
    MultiTypeQueue(const MultiTypeQueue&) = delete;

    /**
     * @brief Delete copy assignment operator
     */
    MultiTypeQueue& operator=(const MultiTypeQueue&) = delete;

    /**
     * @brief Delete move constructor
     */
    MultiTypeQueue(MultiTypeQueue&&) = delete;

    /**
     * @brief Delete move assignment operator
     */
    MultiTypeQueue& operator=(MultiTypeQueue&&) = delete;

    /**
     * @brief Destructor.
     */
    ~MultiTypeQueue() override = default;

    /**
     * @copydoc IMultiTypeQueue::push(Message, bool)
     */
    int push(Message message, bool shouldWait = false) override;

    /**
     * @copydoc IMultiTypeQueue::pushAwaitable(Message)
     */
    boost::asio::awaitable<int> pushAwaitable(Message message) override;

    /**
     * @copydoc IMultiTypeQueue::push(std::vector<Message>)
     */
    int push(std::vector<Message> messages) override;

    /**
     * @copydoc IMultiTypeQueue::getNext(MessageType, const std::string, const std::string)
     */
    Message getNext(MessageType type, const std::string moduleName = "", const std::string moduleType = "") override;

    /**
     * @copydoc IMultiTypeQueue::getNextNAwaitable(MessageType, int, const std::string, const std::string)
     */
    boost::asio::awaitable<std::vector<Message>> getNextNAwaitable(MessageType type,
                                                                   int messageQuantity,
                                                                   const std::string moduleName = "",
                                                                   const std::string moduleType = "") override;

    /**
     * @copydoc IMultiTypeQueue::getNextN(MessageType, int, const std::string, const std::string)
     */
    std::vector<Message> getNextN(MessageType type,
                                  int messageQuantity,
                                  const std::string moduleName = "",
                                  const std::string moduleType = "") override;

    /**
     * @copydoc IMultiTypeQueue::pop(MessageType, const std::string)
     */
    bool pop(MessageType type, const std::string moduleName = "") override;

    /**
     * @copydoc IMultiTypeQueue::popN(MessageType, int, const std::string)
     */
    int popN(MessageType type, int messageQuantity, const std::string moduleName = "") override;

    /**
     * @copydoc IMultiTypeQueue::isEmpty(MessageType, const std::string)
     */
    bool isEmpty(MessageType type, const std::string moduleName = "") override;

    /**
     * @copydoc IMultiTypeQueue::isFull(MessageType, const std::string)
     */
    bool isFull(MessageType type, const std::string moduleName = "") override;

    /**
     * @copydoc IMultiTypeQueue::storedItems(MessageType, const std::string)
     */
    int storedItems(MessageType type, const std::string moduleName = "") override;
};
