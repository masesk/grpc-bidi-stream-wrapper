/*
 *
 * Copyright 2021 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <mutex>

#include "bidi_chat.grpc.pb.h"
#include "grpc_bidi_wrapper.hpp"




int main(int argc, char ** argv) {
  // ask for username first
  std::string username;
  std::cout << "Type your username: ";
  // grab and store the username 
  getline(std::cin, username);

  // create a callback method to announce the messages
  const auto read_callback = [&](bidichat::Message message) {
    std::cout <<  message.user() << ": " << message.message() << std::endl;
  };

  // initialize callback to setup the Chat handler to the client pointer
  // Chat is the name of the RPC and can be changed from here
  auto initialize_callback = [](grpc::ClientContext* context, bidichat::BidiChatStream::Stub* stub, masesk::StreamerClient<bidichat::Message, bidichat::Message, bidichat::BidiChatStream> *client){
    stub->async()->Chat(context, client);
  };

  // create the chat service
  masesk::BidiClientWrapper < bidichat::Message, bidichat::Message, bidichat::BidiChatStream > chat("localhost:50051", read_callback, initialize_callback);

  // create a detached thread that continously asks the user for input
  std::thread writer([ &username, &chat ] {
    std::string input;
    std::cout << "Type your username: ";
    bidichat::Message message;
    std::cout << "Chat has started!" << std::endl;
    message.set_user(username);
    while (input != "quit") {
      getline(std::cin, input);
      message.set_message(input);

      // reliable write means if the server is down, the messages will go into a buffer
      // when the client reconnects, the messages will be sent
      chat.ReliableWrite(message);
      
    }
    chat.Shutdown();
  });
  writer.detach();

  // start the chat stream. This won't quit until shutdown is called.
  chat.StartStreamerClient();


  return 0;
}