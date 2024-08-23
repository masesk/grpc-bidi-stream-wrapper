#pragma once
#ifndef GRPC_CHANNEL_WRAPPER_HPP
#define GRPC_CHANNEL_WRAPPER_HPP

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <chrono>
#include <list>
#include <functional>
#include <queue>

#include <grpc/grpc.h>
#include <grpcpp/alarm.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

namespace masesk
{

    // Thread-safe queue
    namespace ThreadSafe
    {

        template <typename T>
        class Queue
        {
        private:
            // Underlying queue
            std::queue<T> m_queue;

            // mutex for thread synchronization
            std::mutex m_mutex;

            // Condition variable for signaling
            std::condition_variable m_cond;

            // shutdown conditional
            bool shutdown_flag = false;

        public:
            // Pushes an element to the queue
            void push(T item)
            {

                // Acquire lock
                std::unique_lock<std::mutex> lock(m_mutex);

                // Add item
                m_queue.push(item);

                // Notify one thread that
                // is waiting
                m_cond.notify_one();
            }

            // Pops an element off the queue
            bool pop(T &refReturn)
            {

                // acquire lock
                std::unique_lock<std::mutex> lock(m_mutex);

                // wait until queue is not empty
                m_cond.wait(lock);

                if (shutdown_flag)
                    return false;

                // retrieve item
                T item = m_queue.front();
                m_queue.pop();

                refReturn = std::move(item);
                return true;
            }

            void shutdown()
            {
                shutdown_flag = true;
                m_cond.notify_one();
            }

            int size()
            {
                return m_queue.size();
            }
        };
    };

    template <class ReaderType, class WriterType, typename ServiceType>
    class StreamerClient : public grpc::ClientBidiReactor<ReaderType, WriterType>
    {
    public:
        /// @brief Default constructor
        /// @param read_callback - Callback for when messages are recevied. Should only use copy parameters.
        explicit StreamerClient(const std::function<void(ReaderType)> &read_callback) : read_callback_(read_callback)
        {
            context_ = std::make_unique<ClientContext>();
        }
        /// @brief thread safe destructor; simply calls Shutdown
        ~StreamerClient()
        {
            Shutdown();
        }

        /// @brief Kicks off the stream by starting the reader and writer.
        /// @param stub - A pointer of the stub
        void Start(typename ServiceType::Stub *stub)
        {
            // set the context to be ready and not fail fast if it can't conenct
            context_->set_wait_for_ready(true);

            // setup the async to be
            // @todo change Chat to your rpc name
            stub->async()->Chat(context_.get(), this);

            // create writer thread
            std::thread writer_thread([&]
                                      { this->NextWrite(); });

            // start reader by passing the reader item to it
            this->StartRead(&reader_item);

            // add hold to ensure nothing gets destructed until shutdown is called
            this->AddHold();

            // start reader
            this->StartCall();

            // join to ensure it completed at least one cycle
            writer_thread.join();
        }

        /// @brief Creates a new context and re-initialized the contents of the stub
        /// @param stub - A pointer to the stub
        void Reset(typename ServiceType::Stub *stub)
        {
            // cancel current context
            context_->TryCancel();

            // recreate context
            context_ = std::make_unique<ClientContext>();

            // set not to fail when not ready
            context_->set_wait_for_ready(true);

            // assign the callback instane to grpc
            // @TODO: Change BidiStream to your rpc name
            stub->async()->Chat(context_.get(), this);

            // start reading again
            this->StartRead(&reader_item);

            // kick off the callbacks
            this->StartCall();
        }

        /// @brief Thread safe method to cancel all pending operations and notify locks
        void Shutdown()
        {
            // grab a mutext
            std::unique_lock<std::mutex> l(mu_);

            // check if already been shutdown
            if (done_)
                return;
            // set it that it has been shutdown
            done_ = true;
            // signal last write
            this->StartWritesDone();

            // remove hold from thread
            this->RemoveHold();

            // shutdown queue (depends on shutdown flag here)
            queue.shutdown();

            // cancel context for any pending messages
            context_->TryCancel();

            // notify all locks
            cv_.notify_all();
        }
        /// @brief Callback overriden method to ensure writes can be done again.
        /// @param ok - If true, no errors were encountered
        void OnWriteDone(bool ok) override
        {
            if (ok)
            {
                this->NextWrite();
            }
        }
        /// @brief Callback overriden method to receive and trigger the callback method
        /// @param ok - If true, no errors were encountered
        void OnReadDone(bool ok) override
        {
            if (ok)
            {
                read_callback_(reader_item);
                this->StartRead(&reader_item);
            }
        }
        /// @brief Callback overriden method - not used for our purposes since it can be called when a disconnection happens
        /// @param s - Status of the onDone
        void OnDone(const Status &s) override
        {
            // Shutdown();
        }

        /// @brief Thread safe write method to push messages to a queue
        /// @param item - Item to be written
        void Write(const WriterType &item)
        {
            std::unique_lock<std::mutex> l(mu_);
            queue.push(item);
        }

        /// @brief Ensures the operations are completed safely before exiting
        /// @return - Statu of the exit process
        Status Await()
        {
            std::unique_lock<std::mutex> l(mu_);
            cv_.wait(l, [this]
                     { return done_; });
            return std::move(status_);
        }

    private:
        /// @brief Internal method to pop messages off the queue and write them in order
        void NextWrite()
        {
            if (!queue.pop(writer_item))
            {
                return;
            }
            this->StartWrite(&writer_item);
        }

        /// @brief context of the grpc instance
        std::unique_ptr<ClientContext> context_;

        /// @brief Reader item
        ReaderType reader_item;

        /// @brief Writer item
        WriterType writer_item;

        /// @brief Mutex used to ensure thread safe operations in the streamer
        std::mutex mu_;

        /// @brief Used to ensure await does not exit until triggered by a shutdown call
        std::condition_variable cv_;

        /// @brief Current status of the last operation
        Status status_;

        /// @brief Flag to signify if shutdown has been called
        bool done_ = false;

        /// @brief Thread safe
        ThreadSafe::Queue<WriterType> queue;

        /// @brief Callback method to be triggered if a new message arrives
        std::function<void(ReaderType)> read_callback_;
    };

    /// @brief A wrapper for reliable client connection to the server
    /// @tparam ReaderType - gRPC object type that will be read from the server
    /// @tparam WriterType  - gRPC object type that will be written to the server
    /// @tparam ServiceType - Type of the service
    template <class ReaderType, class WriterType, typename ServiceType>
    class BidiClientWrapper
    {
    public:
        /// @brief Default constructor of Wrapper
        /// @param address - Address to be used with grpc, usually in "host:port" format
        /// @param read_callback - Callback function to be triggered when a message arrives from the server
        BidiClientWrapper(const std::string &address, const std::function<void(ReaderType)> read_callback) : address_(address), streamerClient(read_callback), read_callback_(read_callback)
        {
            // constructor calls initialize to setup connection listener. This method can become public if needed
            Initialize();
        }

        /// @brief Kick off the streamer and await for termination
        void StartStreamerClient()
        {
            // start stream and await for end of streamer
            streamerClient.Start(stub_.get());
            Status status = streamerClient.Await();
        }

        /// @brief Shutdown pending threads and streamer
        void Shutdown()
        {

            std::unique_lock<std::mutex> l(mutex);
            // shutdown down state cq first to kill the detached thread
            state_cq.Shutdown();

            // shutdown streamer to stop the writer thread, and notify all locks
            streamerClient.Shutdown();
        }

        /// @brief Reliable write ensures that messages are stored in a queue if the server is down
        /// @param writeItem - Item to be written to the server
        void ReliableWrite(const WriterType &writeItem)
        {
            // exposed methods should lock the common mutex
            std::unique_lock<std::mutex> l(mutex);
            InternalReliableWrite(writeItem);
        }

        /// @brief This method does not guarantee delivery if the server is down
        /// @param writeItem - Item to be written
        void UnreliableWrite(const WriterType &writeItem)
        {
            // exposed methods should lock the common mutex
            std::unique_lock<std::mutex> l(mutex);
            InternalUnreliableWrite(writeItem);
        }

    private:
        /// @brief Initialize the channel and create state listener thread
        void Initialize()
        {
            grpc::ChannelArguments args;

            // change these args as needed
            // below are for keep alive messages
            args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 5 * 1000 /*20 sec*/);
            args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5 * 1000 /*10 sec*/);
            args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);

            // below are for the reconnection retries and timeouts
            args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 2000);
            args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, 2000);
            args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 2000);

            // this is on an insecure channel
            // for production, generate certs to encrypt or authenticate with server
            channel_ = grpc::CreateCustomChannel(address_, grpc::InsecureChannelCredentials(), args);

            // unique stub so that it can be recreated later
            stub_ = std::make_unique<typename ServiceType::Stub>(channel_);

            // create a state notify handler so that we can keep track of our connection
            channel_->NotifyOnStateChange(channel_->GetState(true), std::chrono::system_clock::time_point::max(), &this->state_cq, nullptr);
            notify_thread_ = std::thread([this]
                                         {
        void * tag = nullptr;
        bool ok = false;

        while (this->state_cq.Next( & tag, & ok)) {
          {
            // lock in this scope 
            std::unique_lock < std::mutex > l(mutex);

            // get state
            grpc_connectivity_state state = this -> channel_ -> GetState(true);
            this->channel_->NotifyOnStateChange(state, std::chrono::system_clock::time_point::max(), &this->state_cq, nullptr);
            // if we were ready and now we are not (failed or idle), we have a problem
            if (state_ == GRPC_CHANNEL_READY && (state == GRPC_CHANNEL_IDLE || state == GRPC_CHANNEL_TRANSIENT_FAILURE)) {

              // set our state to connected
              state_ = GRPC_CHANNEL_CONNECTING;

              // recreate the stub with the same channel info
              stub_ = std::make_unique < typename ServiceType::Stub > (channel_);

              // reset the streamer
              streamerClient.Reset(stub_.get());
            }
            else{
              // set our internal state to the new state
              state_ = state;
            }
          }
          
          // if our state is ready, we should flush our queue
          if(state_ == GRPC_CHANNEL_READY){
            // if we are now ready, flush the queue
            FlushQueue();
          }

        } });

            // detach to get execution going
            notify_thread_.detach();
        }

        /// @brief Internal reliable write; can be used if it is called from an already locked thread
        /// @param writeItem - Item to be written
        void InternalReliableWrite(const WriterType &writeItem)
        {
            switch (state_)
            {
            case GRPC_CHANNEL_READY:
                streamerClient.Write(writeItem);
                break;
            default:
                message_queue.push_back(writeItem);
                break;
            }
        }
        /// @brief Internal unreliable write; can be used if it is called from an already locked thread
        /// @param writeItem - Item to be written
        void InternalUnreliableWrite(const WriterType &writeItem)
        {
            streamerClient.Write(writeItem);
        }

        /// @brief Flushes the queue
        void FlushQueue()
        {
            std::unique_lock<std::mutex> l(mutex);
            for (const auto &writeItem : message_queue)
            {
                streamerClient.Write(writeItem);
            }
            message_queue.clear();
        }

        /// @brief A queue used to be notify in case of a state change
        grpc::CompletionQueue state_cq;

        /// @brief Pointer of the stub
        std::unique_ptr<typename ServiceType::Stub> stub_;

        /// @brief Channel pointer
        std::shared_ptr<Channel> channel_;

        /// @brief streamer of the wrapper
        StreamerClient<ReaderType, WriterType, ServiceType> streamerClient;

        /// @brief mutex of the wrapper
        std::mutex mutex;

        /// @brief message queue as list
        std::list<WriterType> message_queue;

        /// @brief reader callback
        std::function<void(ReaderType)> read_callback_;

        /// @brief Current state of the grpc instance; default: connecting
        grpc_connectivity_state state_ = GRPC_CHANNEL_CONNECTING;

        /// @brief notify thread when grpc state changes
        std::thread notify_thread_;

        /// @brief address of the channel
        std::string address_;
    };

    template <class ReaderType, class WriterType, typename ServiceType>
    class StreamerServerClient : public grpc::ServerBidiReactor<ReaderType, WriterType>
    {
    public:
        
        StreamerServerClient(std::mutex &context_mutex, std::list<StreamerServerClient<ReaderType, WriterType, ServiceType> *> &clients,
                             std::function<void(ReaderType, StreamerServerClient<ReaderType, WriterType, ServiceType> *)> &read_callback,
                             grpc::CallbackServerContext *context)
            : context_mutex_(context_mutex), clients_(clients), read_callback_(read_callback), context_(context)
        {
            {
                std::unique_lock<std::mutex> l(context_mutex_);
                clients.push_back(this);
                self_it = clients_.end();
                self_it--;
            }

            // create writer thread
            writer_thread = std::thread([&]
                                      { this->NextWrite(); });

            // start read
            this->StartRead(&reader_item);

            // join to ensure one cycle completed
            writer_thread.detach();
        }

        /// @brief Inidcates if the read operation was done
        /// @param ok - True if the operation has succeeded
        void OnReadDone(bool ok) override
        {
            if (ok)
            {
                // add a scope for the lock. All read callbacks hold a lock in this case
                read_callback_(reader_item, this);
                
                // start next write process
                this->StartRead(&reader_item);
            }
            else
            {
                this->Finish(Status::OK);
            }
        }
        /// @brief Indicates if write operation is finished
        /// @param  - True if operation is successful
        void OnWriteDone(bool ok) override
        {
            if (ok)
            {
                this->NextWrite();
            }
        }
        /// @brief Callback for when the client is done
        /// Deletes instance from list and then deletes self pointer
        void OnDone() override
        {
            // all rpc operations completed. Erase from the list and delete self pointer
            std::unique_lock<std::mutex> l(context_mutex_);
            clients_.erase(self_it);
            delete this;
        }

        /// @brief Add the write item to the queue for next cycle of writes
        /// @param write_item - Protobuff object to be written
        void Write(const WriterType &write_item)
        {
            // hold a lock
            std::unique_lock<std::mutex> l(mutex_);

            queue.push(write_item);
        }

        /// @brief Can be trigger during a callback when the client gets access to pointer
        /// Thread safe due to the order of operation. This method can only be accessible in a
        /// read callback, but the callback holds the mutex
        void Disconnect()
        {
            // hold a lock
            std::unique_lock<std::mutex> l(mutex_);

            // shutdown queue
            queue.shutdown();

            // context_ might be null. Try cancel will eventually stop all read/write ops
            if (context_ != nullptr)
                context_->TryCancel();
                
        }

        /// @brief Cancel operation
        /// @todo figure out behavior
        void OnCancel() override
        {
            // not sure what to do here. Cancelled RPCs are not the same as onDone
        }

    private:
        /// @brief Handle the next write operations
        void NextWrite()
        {
            if (!queue.pop(writer_item))
            {
                return;
            }
            this->StartWrite(&writer_item);
        }
        /// @brief instance to be used for reading data from client
        ReaderType reader_item;

        /// @brief instance to be used for writing data to client
        WriterType writer_item;

        /// @brief mutex used to ensure thread safety
        std::mutex mutex_;

        /// @brief mutex passed by context handler
        std::mutex &context_mutex_;

        /// @brief list of all clients given as reference
        std::list<StreamerServerClient<ReaderType, WriterType, ServiceType> *> &clients_;

        /// @brief a queue of notes to be sent to client
        typename std::deque<WriterType> to_send_notes_;

        /// @brief iterator that points at self for easy access and deletion
        typename std::list<StreamerServerClient<ReaderType, WriterType, ServiceType> *>::iterator self_it;

        /// @brief callback function for new read events
        std::function<void(ReaderType, StreamerServerClient<ReaderType, WriterType, ServiceType> *)> read_callback_;

        /// @brief flag to indicate if the client is disconnected
        bool disconnect = false;

        /// @brief context pointer passed from context handler (BidiServerWrapper)
        grpc::CallbackServerContext *context_;

        /// @brief Thread safe
        ThreadSafe::Queue<WriterType> queue;

        /// @brief thread to write pending messages
        std::thread writer_thread;
    };

    template <class ReaderType, class WriterType, typename ServiceType>
    class BidiServerWrapper final : public ServiceType::CallbackService
    {
    public:
        explicit BidiServerWrapper(const std::function<void(ReaderType, StreamerServerClient<ReaderType, WriterType, ServiceType> *)> read_callback = 0) : read_callback_(read_callback)
        {
        }
        /// @brief Callback used for overriding route chat rpc
        /// @param context - Context generated for this channel
        /// @todo Change Chat to the corresponding RPC name used in proto
        /// @return - a new instance of StreamerServerClient
        grpc::ServerBidiReactor<ReaderType, WriterType> *Chat(
            grpc::CallbackServerContext *context) override
        {
            return new StreamerServerClient<ReaderType, WriterType, ServiceType>(mutex, clients, read_callback_, context);
        }

        /// @brief Write the grpc send type to all clients
        /// @param item - protobuf object to be written
        void Broadcast(const WriterType &item)
        {
            std::unique_lock<std::mutex> l(mutex);
            for (const auto &it : clients)
            {
                it->Write(item);
            }
        }

        void AddCallback(const std::function<void(ReaderType, StreamerServerClient<ReaderType, WriterType, ServiceType> *)> read_callback){
            read_callback_ = read_callback;
        }

    private:
        /// @brief callback for all read operations
        std::function<void(ReaderType, StreamerServerClient<ReaderType, WriterType, ServiceType> *)> read_callback_;

        /// @brief list of connected and active clients
        std::list<StreamerServerClient<ReaderType, WriterType, ServiceType> *> clients;

        /// @brief mutex
        std::mutex mutex;
    };

};

#endif