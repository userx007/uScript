// grpc_loopback_server.cpp
//
// A real gRPC server implementing LoopbackService (see loopback.proto):
// unary, server-streaming, client-streaming and bidi-streaming RPCs, each
// doing the simplest possible echo, so a script exercising the GRPC
// plugin can assert exact round-trip data for every call shape without
// needing a real target service. This is the GRPC plugin's analog of
// tcpip_loopback_server.cpp / udp_loopback_server.cpp / the WebSocket
// tester's echo server - same purpose, deliberately different means: those
// hand-roll their wire protocol in raw sockets specifically to avoid a
// build-time dependency on a real library; gRPC has no such option (there
// is no "raw gRPC socket" to hand-roll against) and the plugin itself
// already requires real grpc++/libprotobuf to build at all, so this test
// server links against them directly rather than reimplementing HTTP/2
// framing and the protobuf wire format from scratch.
//
// Build (see build.sh, which also produces the .protoset the plugin
// itself needs):
//
//   protoc --cpp_out=. --grpc_out=.
//       --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) loopback.proto
//   g++ -std=c++17 -O2 -Wall -Wextra -o grpc_loopback_server
//       grpc_loopback_server.cpp loopback.pb.cc loopback.grpc.pb.cc
//       $(pkg-config --cflags --libs grpc++ protobuf)
//
// Usage:
//   grpc_loopback_server [port] [bind_address]
//
//   port          TCP port to listen on (default: 50051, the same default
//                 grpc's own "helloworld" examples use)
//   bind_address  Address to bind to (default: "0.0.0.0")
//
// Behaviour:
//   - Every RPC is a plain echo (see loopback.proto's own doc comments for
//     exactly what each one does) - there is no state shared between
//     separate calls beyond a per-call-shape counter used to tag replies,
//     and no persistence across a restart.
//   - Logs each call (shape, request, and for streaming calls each
//     message) to stdout.
//   - Multi-threaded (grpc++'s default sync server dispatch): concurrent
//     calls from multiple GRPC plugin instances are handled independently.
//   - Ctrl+C (SIGINT) or SIGTERM shuts the server down cleanly.
//
// Try it once running (see run.sh), from a script using the GRPC plugin
// (see docs/grpc_plugin_tutorial.md for the full syntax):
//
//   LOAD_PLUGIN GRPC
//   GRPC.CONFIG h=127.0.0.1 p=50051 d=loopback.protoset
//   resp ?= GRPC.CMD > 'CALL loopback.LoopbackService/Echo {"text":"hi"}' | R'.*'
//   LOG.PRINT $resp   // {"text":"hi","callNumber":1}

#include "loopback.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

namespace
{
    constexpr int         DEFAULT_PORT = 50051;
    constexpr const char* DEFAULT_BIND = "0.0.0.0";

    std::unique_ptr<grpc::Server> g_server;

    void on_signal(int /*sig*/)
    {
        // Server::Shutdown() is safe to call from a signal handler's
        // context here since it just posts to the completion queue the
        // server's own accept loop is already waiting on; Wait() below
        // then returns and main() exits normally.
        if (g_server) {
            g_server->Shutdown();
        }
    }

    class LoopbackServiceImpl final : public loopback::LoopbackService::Service
    {
    public:
        grpc::Status Echo(grpc::ServerContext*, const loopback::EchoRequest* request,
                           loopback::EchoResponse* response) override
        {
            const int32_t callNumber = ++m_echoCounter;
            std::printf("[Echo] #%d text=\"%s\"\n", callNumber, request->text().c_str());
            response->set_text(request->text());
            response->set_call_number(callNumber);
            return grpc::Status::OK;
        }

        grpc::Status Ping(grpc::ServerContext*, const loopback::PingRequest*,
                           loopback::PingResponse* response) override
        {
            std::printf("[Ping]\n");
            response->set_ok(true);
            return grpc::Status::OK;
        }

        grpc::Status EchoStream(grpc::ServerContext*, const loopback::EchoStreamRequest* request,
                                 grpc::ServerWriter<loopback::EchoResponse>* writer) override
        {
            const int32_t count = request->count() > 0 ? request->count() : 1;
            std::printf("[EchoStream] text=\"%s\" count=%d\n", request->text().c_str(), count);
            for (int32_t i = 1; i <= count; ++i) {
                loopback::EchoResponse response;
                response.set_text(request->text());
                response.set_call_number(i);
                writer->Write(response);
            }
            return grpc::Status::OK;
        }

        grpc::Status EchoCollect(grpc::ServerContext*, grpc::ServerReader<loopback::EchoRequest>* reader,
                                  loopback::EchoResponse* response) override
        {
            loopback::EchoRequest request;
            std::ostringstream joined;
            int32_t received = 0;
            bool first = true;
            while (reader->Read(&request)) {
                if (!first) joined << ", ";
                joined << request.text();
                first = false;
                ++received;
            }
            std::printf("[EchoCollect] received=%d joined=\"%s\"\n", received, joined.str().c_str());
            response->set_text(joined.str());
            response->set_call_number(received);
            return grpc::Status::OK;
        }

        grpc::Status EchoChat(grpc::ServerContext*,
                               grpc::ServerReaderWriter<loopback::EchoResponse, loopback::EchoRequest>* stream) override
        {
            loopback::EchoRequest request;
            int32_t callNumber = 0;
            while (stream->Read(&request)) {
                ++callNumber;
                std::printf("[EchoChat] #%d text=\"%s\"\n", callNumber, request.text().c_str());
                loopback::EchoResponse response;
                response.set_text(request.text());
                response.set_call_number(callNumber);
                stream->Write(response);
            }
            std::printf("[EchoChat] client half-closed after %d message(s)\n", callNumber);
            return grpc::Status::OK;
        }

    private:
        std::atomic<int32_t> m_echoCounter{0};
    };
} // namespace

int main(int argc, char** argv)
{
    const int iPort = (argc > 1) ? std::atoi(argv[1]) : DEFAULT_PORT;
    const std::string strBindTo = (argc > 2) ? argv[2] : DEFAULT_BIND;

    if (iPort <= 0 || iPort > 65535) {
        std::fprintf(stderr, "Invalid port: %s\n", (argc > 1) ? argv[1] : "");
        return 1;
    }

    struct sigaction sSigAction = {};
    sSigAction.sa_handler = on_signal;
    sSigAction.sa_flags   = 0;
    ::sigemptyset(&sSigAction.sa_mask);
    ::sigaction(SIGINT, &sSigAction, nullptr);
    ::sigaction(SIGTERM, &sSigAction, nullptr);

    const std::string strAddress = strBindTo + ":" + std::to_string(iPort);

    LoopbackServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(strAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    g_server = builder.BuildAndStart();
    if (!g_server) {
        std::fprintf(stderr, "Failed to start server on %s\n", strAddress.c_str());
        return 1;
    }

    std::printf("grpc_loopback_server listening on %s (LoopbackService: Echo, Ping, EchoStream, "
                "EchoCollect, EchoChat)\n", strAddress.c_str());

    g_server->Wait(); // returns once Shutdown() runs, from on_signal()

    std::printf("grpc_loopback_server stopped\n");
    return 0;
}
