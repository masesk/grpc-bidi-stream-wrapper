#include <memory>
#include <thread>
#include <iostream>

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include "bidi_chat.grpc.pb.h"
#include "grpc_bidi_wrapper.hpp"


class BidiServer : public bidichat::BidiChatStream::CallbackService{
  public:
  BidiServer(masesk::BidiServerWrapper<bidichat::Message, bidichat::Message,bidichat::BidiChatStream> &wrapper): wrapper_(wrapper) {

  }
  grpc::ServerBidiReactor<bidichat::Message, bidichat::Message> *Chat(
            grpc::CallbackServerContext *context) override
        {
          return wrapper_.Initialize(context);
        }
  private:
    masesk::BidiServerWrapper<bidichat::Message, bidichat::Message,bidichat::BidiChatStream> &wrapper_;
};


void RunServer() {
  // server address
  std::string server_address("0.0.0.0:50051");
  // initialize service
  masesk::BidiServerWrapper<bidichat::Message, bidichat::Message,bidichat::BidiChatStream>  wrapper;

  BidiServer service(wrapper);

  // create a callback. Must contain a message and pointer to StreamServerClient pointer in callback
  auto reader_callback = [&wrapper](bidichat::Message note, masesk::StreamerServerClient<bidichat::Message, bidichat::Message,bidichat::BidiChatStream> *client) {
    // when a message arrives, simply broadcast it back to all teh clients
    wrapper.Broadcast(note);
  };

  // add this callback after initialization since callback uses service
  wrapper.AddCallback(reader_callback);

  // build channel
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS,
                             10 * 60 * 1000 /*10 min*/);
  builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS,
                             5 * 1000 /*20 sec*/);
  builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
  builder.AddChannelArgument(
      GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS,
      10 * 1000 /*10 sec*/);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::thread([&server, &wrapper] {
    
    bidichat::Message message;
    message.set_message("Hello from server!");
    while(true){
      std::this_thread::sleep_for(std::chrono::seconds(1));
      wrapper.Broadcast(message);
    }

  }).detach();
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  return 0;
}