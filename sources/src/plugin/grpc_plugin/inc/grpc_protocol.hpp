#ifndef GRPC_PROTOCOL_HPP
#define GRPC_PROTOCOL_HPP

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>

#include <memory>
#include <string>

/**
 * @brief Pure protobuf glue: descriptor loading, service/method resolution,
 * dynamic message construction and JSON<->message conversion. No networking
 * at all — this plays the same role MqttProtocol.hpp/cpp plays for MQTT
 * (pure packet encode/decode, no I/O), except here there is no wire framing
 * left to hand-write: HTTP/2 framing and the protobuf wire format are
 * already implemented by the real grpc++/libprotobuf libraries GrpcDriver
 * links against (see uCommandExec.hpp's own convention of wrapping the
 * real, already-existing driver rather than reimplementing it — that
 * applies here to the whole gRPC/protobuf stack, not just one transport).
 *
 * -----------------------------------------------------------------------
 * Why "dynamic" messages instead of generated C++ classes
 * -----------------------------------------------------------------------
 * A normal gRPC C++ client links against code generated at build time from
 * one specific .proto (protoc --cpp_out/--grpc_out), and can therefore only
 * ever talk to that one service. That doesn't fit this codebase's plugin
 * model: UART talks to anything on a serial line, TCPIP to anything on a
 * socket, MQTT to any MQTT v3.1.1 broker — one plugin, config picks the
 * target. For GRPC to follow that same convention it must be able to call
 * an arbitrary method on an arbitrary service *without* being recompiled
 * per target, which means resolving the request/response message shapes at
 * runtime instead of at compile time.
 *
 * This is exactly what protobuf's reflection API is for, and it's the same
 * mechanism grpcurl/grpc_cli use: a FileDescriptorSet (produced ahead of
 * time by `protoc --descriptor_set_out=service.protoset --include_imports
 * service.proto`, and pointed to via GRPC.CONFIG's d= setting) is loaded
 * into a DescriptorPool; a DynamicMessageFactory then builds a concrete
 * google::protobuf::Message for any Descriptor out of that pool, with no
 * generated class involved at all. GrpcDriver later hands that Message,
 * still only known by its base-class type, straight to
 * grpc::internal::BlockingUnaryCall<Message,Message>() — grpc++'s
 * SerializationTraits for protobuf messages only cares that the type
 * derives from MessageLite, so this works for a dynamically-built message
 * exactly as it would for a generated one.
 *
 * -----------------------------------------------------------------------
 * The GRPC.CMD command line and real, unmodified JSON
 * -----------------------------------------------------------------------
 * The shared grammar this text arrives through (uCommScriptCommandValidator.hpp)
 * has two independent quoting mechanisms that are easy to conflate: a
 * *field-boundary* scanner (splitCommandBody()) that toggles on literal
 * `"` characters purely to let a field safely contain `|`/`~` without being
 * mistaken for a separator, and a completely separate *decorator* syntax
 * (classify()) that identifies R'...'/H'...'/T'.../'...' etc. by a leading
 * **single quote `'`** (see DECORATOR_STRING_START/DECORATOR_REGEX_START in
 * uSharedConfig.hpp) — not the double quote a C/JSON reader would expect.
 * undecorate() for a matched decorator is a plain prefix/suffix character
 * strip with no restriction on what's inside, so wrapping GRPC.CMD's whole
 * `CALL ...` text in real single quotes is enough to carry a real JSON
 * request body — double quotes, colons, braces and all — through
 * unmodified, with no character-substitution convention needed:
 *
 *     GRPC.CMD > 'CALL greeter.Greeter/SayHello {"name":"world"}'
 *     resp ?= GRPC.CMD > 'CALL greeter.Greeter/SayHello {"name":"world"}' | '{"message":"Hello, world!"}'
 *
 * (A JSON object's `"` characters always come in matched pairs, so they
 * toggle splitCommandBody()'s insideQuote tracking back to its starting
 * state by the time the field ends — the ` | ` separator after the closing
 * `'` above is still found correctly.) A method whose request message has
 * every field optional (or none at all, e.g. `google.protobuf.Empty`) may
 * omit the JSON body entirely: `GRPC.CMD > 'CALL pkg.Health/Check'`.
 */
class GrpcProtocol
{
public:
    GrpcProtocol() = default;

    /**
     * @brief Load a FileDescriptorSet (.protoset, see class doc comment)
     *        from disk and build every file it contains into this
     *        instance's DescriptorPool. Call once, before resolving any
     *        method — mirrors MqttDriver::open()'s one-time session setup.
     * @return true if the file was read and every contained descriptor
     *         built successfully.
     */
    bool loadDescriptorSet(const std::string& protosetPath, std::string& outError);

    /**
     * @brief Resolve "package.Service/Method" (or "package.Service.Method",
     *        both are accepted) into the corresponding MethodDescriptor.
     * @return nullptr if the descriptor set has no such service/method.
     *         Every RPC shape (unary, server-streaming, client-streaming,
     *         bidi) is returned normally; grpc_driver.hpp's send()
     *         dispatches on method->client_streaming()/server_streaming()
     *         to decide which of the four it's doing.
     */
    const google::protobuf::MethodDescriptor* resolveMethod(const std::string& methodPath,
                                                              std::string& outError) const;

    /** @brief Build a fresh, empty, writable request message for a method. */
    std::unique_ptr<google::protobuf::Message> newRequestMessage(
        const google::protobuf::MethodDescriptor* method) const;

    /** @brief Build a fresh, empty, writable response message for a method. */
    std::unique_ptr<google::protobuf::Message> newResponseMessage(
        const google::protobuf::MethodDescriptor* method) const;

    /**
     * @brief Parse real JSON text (see class doc comment) into an
     *        already-allocated message. `message` is cleared first, so a
     *        failed parse never leaves partial fields. An empty
     *        `jsonText` is treated as `{}` (every field defaulted).
     */
    bool parseJsonIntoMessage(const std::string& jsonText, google::protobuf::Message& message,
                               std::string& outError) const;

    /** @brief Serialize a message to real JSON text. */
    bool messageToJson(const google::protobuf::Message& message, std::string& outText) const;

private:
    // Owns every descriptor built from the loaded .protoset; the pool (and
    // therefore every MethodDescriptor/Descriptor handed out) lives exactly
    // as long as this GrpcProtocol instance does, which — like MqttDriver's
    // TCPIP session — is the whole plugin session (see grpc_plugin.hpp's
    // "Session lifetime").
    google::protobuf::DescriptorPool m_pool;
    mutable google::protobuf::DynamicMessageFactory m_factory{&m_pool};
    bool m_bLoaded = false;
};

#endif // GRPC_PROTOCOL_HPP
